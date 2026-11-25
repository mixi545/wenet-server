// g++ -std=c++17 -pthread -I. tests/console-file-log-test.cc -o /tmp/log-test
#include "sherpa-onnx/csrc/console-file-log.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

static std::string Read(const std::string &path) {
  std::ifstream in(path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main() {
  char directory[] = "/tmp/ug-log-test-XXXXXX";
  assert(mkdtemp(directory));
  const std::string path = std::string(directory) + "/ug.log";
  const std::string console = std::string(directory) + "/console";
  const int original_out = dup(STDOUT_FILENO);
  const int capture = open(console.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  assert(original_out >= 0 && capture >= 0);
  assert(dup2(capture, STDOUT_FILENO) >= 0);
  close(capture);

  sherpa_onnx::ConsoleFileLog log;
  assert(!log.Start(std::string(directory) + "/missing/ug.log"));
  assert(errno == ENOENT);
  assert(log.Start(path));
  printf("C stdout\n");
  fprintf(stderr, "C stderr\n");
  std::cout << "C++ stdout\n";
  std::cerr << "C++ stderr\n";
  std::clog << "C++ clog\n";
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([i]() {
      for (int j = 0; j < 1000; ++j) {
        fprintf(i % 2 ? stderr : stdout, "thread=%d message=%d\n", i, j);
      }
    });
  }
  for (auto &thread : threads) thread.join();
  log.Stop();
  assert(log.Start(path));
  printf("appended after restart\n");
  log.Stop();
  log.Stop();
  assert(dup2(original_out, STDOUT_FILENO) >= 0);
  close(original_out);

  const std::string contents = Read(path);
  assert(contents == Read(console));
  for (const char *line : {"C stdout\n", "C stderr\n", "C++ stdout\n",
                          "C++ stderr\n", "C++ clog\n",
                          "appended after restart\n"}) {
    assert(contents.find(line) != std::string::npos);
  }
  assert(std::count(contents.begin(), contents.end(), '\n') == 4006);
  unlink(path.c_str());
  unlink(console.c_str());
  rmdir(directory);
  puts("console-file-log test passed");
}
