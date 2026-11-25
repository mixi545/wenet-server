// sherpa-onnx/csrc/online-websocket-server.cc
//
// Copyright (c)  2022-2023  Xiaomi Corporation

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "asio.hpp"  // NOLINT
#include "sherpa-onnx/csrc/console-file-log.h"
#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/online-websocket-server-impl.h"
#include "sherpa-onnx/csrc/parse-options.h"

#include "sherpa-onnx/csrc/sdk-helper.h"

static constexpr const char *kUsageMessage = R"(
Automatic speech recognition with sherpa-onnx using websocket.

Usage:

./bin/sherpa-onnx-online-websocket-server --help

./bin/sherpa-onnx-online-websocket-server \
  --port=6006 \
  --num-work-threads=5 \
  --tokens=/path/to/tokens.txt \
  --encoder=/path/to/encoder.onnx \
  --decoder=/path/to/decoder.onnx \
  --joiner=/path/to/joiner.onnx \
  --log-file=./ug.log \
  --max-batch-size=5 \
  --loop-interval-ms=10

Please refer to
https://k2-fsa.github.io/sherpa/onnx/pretrained_models/index.html
for a list of pre-trained models to download.
)";

int32_t main(int32_t argc, char *argv[]) {
  sherpa_onnx::ParseOptions po(kUsageMessage);

  sherpa_onnx::OnlineWebsocketServerConfig config;

  // the server will listen on this port
  int32_t port = 6006;

  // size of the thread pool for handling network connections
  int32_t num_io_threads = 1;

  // size of the thread pool for neural network computation and decoding
  int32_t num_work_threads = 3;

  po.Register("num-io-threads", &num_io_threads,
              "Thread pool size for network connections.");

  po.Register("num-work-threads", &num_work_threads,
              "Thread pool size for for neural network "
              "computation and decoding.");

  po.Register("port", &port, "The port on which the server will listen.");

  std::string license;
  po.Register("license", &license, "license file"); 

  std::string force_restart_time;
  po.Register("force-restart-time", &force_restart_time,
              "Daily restart time in Asia/Shanghai (HH:MM); disabled by default.");

  config.Register(&po);

  if (argc == 1) {
    po.PrintUsage();
    exit(EXIT_FAILURE);
  }

  po.Read(argc, argv);

#if defined(__linux__)
  sherpa_onnx::ConsoleFileLog log;
  if (!log.Start(config.log_file)) {
    std::fprintf(stderr, "Cannot open log file %s: %s\n",
                 config.log_file.c_str(), std::strerror(errno));
    return EXIT_FAILURE;
  }
#endif

  if (po.NumArgs() != 0) {
    SHERPA_ONNX_LOGE("Unrecognized positional arguments!");
    po.PrintUsage();
    return EXIT_FAILURE;
  }

  int restart_hour = 0;
  int restart_minute = 0;
  if (!force_restart_time.empty()) {
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (force_restart_time.size() != 5 || force_restart_time[2] != ':' ||
        !digit(force_restart_time[0]) || !digit(force_restart_time[1]) ||
        !digit(force_restart_time[3]) || !digit(force_restart_time[4])) {
      SHERPA_ONNX_LOGE("Invalid --force-restart-time: expected HH:MM");
      return EXIT_FAILURE;
    }
    restart_hour = (force_restart_time[0] - '0') * 10 +
                   force_restart_time[1] - '0';
    restart_minute = (force_restart_time[3] - '0') * 10 +
                     force_restart_time[4] - '0';
    if (restart_hour > 23 || restart_minute > 59) {
      SHERPA_ONNX_LOGE("Invalid --force-restart-time: expected HH:MM");
      return EXIT_FAILURE;
    }
#if !defined(__linux__)
    SHERPA_ONNX_LOGE("--force-restart-time requires Linux");
    return EXIT_FAILURE;
#endif
  }

  bool re = valid_check(license.data(), "");
  if (!re) {
    SHERPA_ONNX_LOGE("license is error!");
    return EXIT_FAILURE;
  }

  config.Validate();

  asio::io_context io_conn;  // for network connections
  asio::io_context io_work;  // for neural network and decoding

  sherpa_onnx::OnlineWebsocketServer server(io_conn, io_work, config);
  server.Run(port);

#if defined(__linux__)
  asio::steady_timer restart_timer(io_conn);
  asio::steady_timer wait_timer(io_conn);
  std::function<void()> wait_for_connections;
  if (!force_restart_time.empty()) {
    // shortcut: Shanghai currently uses UTC+8, use a timezone database if its rules change.
    std::time_t shanghai_now = std::time(nullptr) + 8 * 60 * 60;
    std::tm shanghai{};
    if (gmtime_r(&shanghai_now, &shanghai) == nullptr) {
      SHERPA_ONNX_LOGE("Cannot calculate restart time");
      return EXIT_FAILURE;
    }
    int delay = (restart_hour - shanghai.tm_hour) * 3600 +
                (restart_minute - shanghai.tm_min) * 60 - shanghai.tm_sec;
    if (delay <= 0) delay += 24 * 60 * 60;
    restart_timer.expires_after(std::chrono::seconds(delay));
    restart_timer.async_wait([&](const asio::error_code &ec) {
      if (ec) return;
      server.BeginRestart();
      auto deadline = std::chrono::steady_clock::now() +
                      std::chrono::seconds(30);
      wait_for_connections = [&, deadline]() {
        size_t remaining = server.ActiveConnections();
        if (remaining == 0 || std::chrono::steady_clock::now() >= deadline) {
          if (remaining != 0) server.ForceCloseConnections();
          SHERPA_ONNX_LOGE("Scheduled restart, remaining connections: %zu",
                           remaining);
          log.Stop();
          execvp(argv[0], argv);
          const int error = errno;
          log.Start(config.log_file);
          SHERPA_ONNX_LOGE("Restart failed: %s", std::strerror(error));
          log.Stop();
          std::exit(EXIT_FAILURE);
        }
        wait_timer.expires_after(std::chrono::milliseconds(100));
        wait_timer.async_wait([&](const asio::error_code &wait_ec) {
          if (!wait_ec) wait_for_connections();
        });
      };
      wait_for_connections();
    });
    SHERPA_ONNX_LOGE("Daily restart scheduled for %s Asia/Shanghai",
                     force_restart_time.c_str());
  }
#endif

  SHERPA_ONNX_LOGE("Started!");
  SHERPA_ONNX_LOGE("Listening on: %d", port);
  SHERPA_ONNX_LOGE("Number of work threads: %d", num_work_threads);

  // give some work to do for the io_work pool
  auto work_guard = asio::make_work_guard(io_work);

  std::vector<std::thread> io_threads;

  // decrement since the main thread is also used for network communications
  for (int32_t i = 0; i < num_io_threads - 1; ++i) {
    io_threads.emplace_back([&io_conn]() { io_conn.run(); });
  }

  std::vector<std::thread> work_threads;
  for (int32_t i = 0; i < num_work_threads; ++i) {
    work_threads.emplace_back([&io_work]() { io_work.run(); });
  }

  io_conn.run();

  for (auto &t : io_threads) {
    t.join();
  }

  for (auto &t : work_threads) {
    t.join();
  }

  return 0;
}
