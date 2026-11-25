#pragma once 

#include <iostream>
#include <csignal>
#include <execinfo.h>
#include <cstdlib>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <sys/resource.h>

#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>   // _mkdir
#define mkdir_func(path, mode) _mkdir(path)
#else
#include <unistd.h>   // mkdir
#define mkdir_func(path, mode) mkdir(path, mode)
#endif

#ifndef __ANDROID__
#include <libunwind.h>
#include <cxxabi.h>
#endif

#include "spdlog/spdlog.h"
#include "spdlog/sinks/daily_file_sink.h"

namespace encap {

const char* get_current_date() {
  static char buf[16];
  time_t now = time(nullptr);
  strftime(buf, sizeof(buf), "%Y%m%d", localtime(&now));
  return buf;
}

#ifndef __ANDROID__
// /*
void crash_handler(int sig) {
  signal(sig, SIG_DFL);
  unw_cursor_t  cursor;
  unw_context_t context;
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  std::fprintf(stderr, "\n!!! Caught signal %d\n", sig);
  while (unw_step(&cursor) > 0) {
    unw_word_t offset, pc;
    unw_get_reg(&cursor, UNW_REG_IP, &pc);
    if (pc == 0) break;

    char sym[512] = {"??"};
    unw_get_proc_name(&cursor, sym, sizeof(sym), &offset);

    // demangle C++ 符号
    int status;
    char* name = abi::__cxa_demangle(sym, nullptr, nullptr, &status);
    std::fprintf(stderr, "0x%lx %s+%lu\n", pc,
                 name ? name : sym, offset);
    std::free(name);
  }
  raise(SIGSEGV);
}
// */
 /*
void crash_handler(int sig) {
  constexpr size_t N = 128;
  void* buf[N];

  signal(sig, SIG_DFL);

  const char hdr[] = "\n!!! Caught signal ";
  write(STDERR_FILENO, hdr, sizeof(hdr)-1);

  char sigbuf[64];
  int  len = snprintf(sigbuf, sizeof(sigbuf), "%d\n", sig);
  write(STDERR_FILENO, sigbuf, len);

  int cnt = backtrace(buf, N);
  backtrace_symbols_fd(buf, cnt, STDERR_FILENO);   // 直接写 fd，不 malloc

  // 3. 生成 core 文件
  raise(SIGSEGV);
}
// */

void coredump_handler(int sig) {
  std::signal(sig, SIG_DFL);

  void* buffer[64];
  int cnt = backtrace(buffer, 64);
  char** symbols = backtrace_symbols(buffer, cnt);

  std::cerr << "\n!!! Caught signal " << sig << " (" << strsignal(sig) << ")\n";
  for (int i = 0; i < cnt; ++i)
    std::cerr << symbols[i] << '\n';
  std::free(symbols);

  abort();
}

void install_coredump_handlers() {
  struct rlimit rl = {RLIM_INFINITY, RLIM_INFINITY};
  setrlimit(RLIMIT_CORE, &rl);
  
  std::signal(SIGSEGV, crash_handler);
  std::signal(SIGABRT, crash_handler);
  std::signal(SIGILL,  crash_handler);
  std::signal(SIGQUIT, crash_handler);
  std::signal(SIGTRAP, crash_handler);
}


std::shared_ptr<spdlog::logger> spd_logger;
void create_spdloger_handle() {
  spd_logger = spdlog::daily_logger_mt(
    "daily_logger",
    "sherpa-onnx-daily.log",
    2, 0);
  
  spd_logger->error("date-based rolling");
}
#endif

std::vector<std::string> split_path(const std::string& path) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : path) {
    if (c == '/' || c == '\\') {
        if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    } else cur += c;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool mkdir_recursive(const std::string& path) {
  std::string build;
#ifdef _WIN32
  if (!path.empty() && path[0] != '/' 
            && path[0] != '\\' 
            && path.size() > 2 
            && path[1] == ':')
    build = path.substr(0, 3); // keep "C:/"
#else
  if (!path.empty() && path[0] == '/') build = "/";
#endif
  std::vector<std::string> segs = split_path(path);
  for (size_t i = 0; i < segs.size(); ++i) {
    build += segs[i];
    struct stat st{};
    if (stat(build.c_str(), &st) != 0) {
      if (mkdir_func(build.c_str(), 0755) != 0)
        return false;
    } else if (!S_ISDIR(st.st_mode)) {
      return false;
    }
    build += '/';
  }
  return true;
}

bool create_dir_if_not_exist(const std::string& path) {
  struct stat st{};
  if (stat(path.c_str(), &st) == 0)
      return S_ISDIR(st.st_mode);
  return mkdir_recursive(path);
}

}


