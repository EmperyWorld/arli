#include "job_channel.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
using Clock = std::chrono::steady_clock;
int main() {
  int failed = 0;
  auto check = [&](bool ok, const char* message) { if (!ok) { ++failed; std::cerr << message << '\n'; } };
  int pair[2]; if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return 2;
  int capacity = 1024; setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity));
  arli::JobChannel sender(pair[0], 150); std::string large(1024 * 1024, 'x');
  auto start = Clock::now();
  check(!sender.transfer(large.data(), large.size(), true), "non-reading peer cannot block send forever");
  check(Clock::now() - start < std::chrono::seconds(1), "send obeys deadline");
  close(pair[0]); close(pair[1]);
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return 2;
  std::thread slow([&] {
    for (int n = 0; n < 6; ++n) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      const char byte = 'x'; send(pair[1], &byte, 1, MSG_NOSIGNAL);
    }
  });
  arli::JobChannel receiver(pair[0], 150); char buffer[6]; start = Clock::now();
  check(receiver.transfer(buffer, 1, false), "first partial message arrives");
  check(!receiver.transfer(buffer + 1, 5, false), "progress does not renew whole-handshake deadline");
  check(Clock::now() - start < std::chrono::milliseconds(500), "receive deadline bounded across calls");
  slow.join(); close(pair[0]); close(pair[1]);
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return 2;
  close(pair[1]); arli::JobChannel closed(pair[0], 150);
  check(!closed.transfer(buffer, 1, false), "closed peer returns failure");
  close(pair[0]); return failed ? 1 : 0;
}
