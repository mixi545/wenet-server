#ifndef SHERPA_ONNX_CSRC_CONSOLE_FILE_LOG_H_
#define SHERPA_ONNX_CSRC_CONSOLE_FILE_LOG_H_

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

namespace sherpa_onnx {

// Capture C stdio, C++ streams and library diagnostics in the same log.
class ConsoleFileLog {
 public:
  ConsoleFileLog() = default;
  ConsoleFileLog(const ConsoleFileLog &) = delete;
  ConsoleFileLog &operator=(const ConsoleFileLog &) = delete;
  ~ConsoleFileLog() { Stop(); }

  bool Start(const std::string &path) {
    file_ = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (file_ < 0) return false;
    stdout_ = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    stderr_ = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
    if (stdout_ < 0 || stderr_ < 0 || pipe2(pipe_, O_CLOEXEC) != 0) {
      return Fail(errno);
    }
    try {
      reader_ = std::thread([this]() { Copy(); });
    } catch (const std::system_error &e) {
      return Fail(e.code().value());
    }
    std::cout.flush();
    std::cerr.flush();
    std::clog.flush();
    std::fflush(nullptr);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (dup2(pipe_[1], STDOUT_FILENO) < 0 ||
        dup2(pipe_[1], STDERR_FILENO) < 0) {
      return Fail(errno);
    }
    close(pipe_[1]);
    pipe_[1] = -1;
    return true;
  }

  // Drain before exec: the reader thread does not survive a process restart.
  void Stop() {
    std::cout.flush();
    std::cerr.flush();
    std::clog.flush();
    std::fflush(nullptr);
    if (stdout_ >= 0) dup2(stdout_, STDOUT_FILENO);
    if (stderr_ >= 0) dup2(stderr_, STDERR_FILENO);
    Close(&pipe_[1]);
    if (reader_.joinable()) reader_.join();
    Close(&pipe_[0]);
    Close(&stdout_);
    Close(&stderr_);
    Close(&file_);
  }

 private:
  static void Close(int *fd) {
    if (*fd >= 0) close(*fd);
    *fd = -1;
  }

  bool Fail(int error) {
    Stop();
    errno = error;
    return false;
  }

  static bool WriteAll(int fd, const char *data, size_t size) {
    while (size != 0) {
      ssize_t n = write(fd, data, size);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return false;
      data += n;
      size -= n;
    }
    return true;
  }

  void Copy() {
    char buffer[8192];
    for (;;) {
      ssize_t n = read(pipe_[0], buffer, sizeof(buffer));
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) break;
      if (file_ >= 0 && !WriteAll(file_, buffer, n)) {
        const int error = errno;
        const std::string message = std::string("Cannot write log file: ") +
                                    std::strerror(error) + "\n";
        WriteAll(stderr_, message.data(), message.size());
        Close(&file_);
      }
      WriteAll(stdout_, buffer, n);
    }
  }

  int file_ = -1;
  int stdout_ = -1;
  int stderr_ = -1;
  int pipe_[2] = {-1, -1};
  std::thread reader_;
};

}  // namespace sherpa_onnx
#endif  // __linux__
#endif  // SHERPA_ONNX_CSRC_CONSOLE_FILE_LOG_H_
