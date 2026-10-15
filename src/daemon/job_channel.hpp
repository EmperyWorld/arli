#pragma once
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <poll.h>
#include <sys/socket.h>
namespace arli {
// One deadline for the entire startup exchange; progress never extends it.
class JobChannel {
 public:
  explicit JobChannel(int fd, int timeout_ms = 5000)
      : fd_(fd), deadline_(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms)) {}
  bool transfer(void* buffer, size_t size, bool sending) const {
    auto* bytes = static_cast<char*>(buffer);
    while (size) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - std::chrono::steady_clock::now()).count();
      if (remaining <= 0) return false;
      pollfd p{fd_, static_cast<short>(sending ? POLLOUT : POLLIN), 0};
      const int ready = poll(&p, 1, static_cast<int>(remaining));
      if (ready < 0 && errno == EINTR) continue;
      if (ready <= 0) return false;
      const auto n = sending ? send(fd_, bytes, size, MSG_NOSIGNAL | MSG_DONTWAIT)
                             : recv(fd_, bytes, size, MSG_DONTWAIT);
      if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
      if (n <= 0) return false;
      bytes += n; size -= static_cast<size_t>(n);
    }
    return true;
  }
 private:
  int fd_;
  std::chrono::steady_clock::time_point deadline_;
};
}
