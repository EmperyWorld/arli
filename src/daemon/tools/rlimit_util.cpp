#include "rlimit_util.hpp"

#include <unistd.h>

#include <sys/resource.h>

namespace arli {

void apply_agent_rlimits() {
  struct rlimit rl{};
#ifndef ARLI_SANITIZER_BUILD
  // Sanitizers reserve a multi-terabyte sparse shadow mapping.  A virtual
  // address limit prevents their runtime from starting even though the
  // mapping does not consume that amount of physical memory.
  rl.rlim_cur = rl.rlim_max = 8ULL << 30;
  setrlimit(RLIMIT_AS, &rl);
#endif
  rl.rlim_cur = rl.rlim_max = 7200;
  setrlimit(RLIMIT_CPU, &rl);
}

void apply_shell_rlimits() {
  struct rlimit rl{};
  // V8 reserves address space at startup; 512 MiB crashes even trivial Node checks.
  // This is a virtual-address limit, not a reservation of physical RAM.
  rl.rlim_cur = rl.rlim_max = 4ULL << 30;
  setrlimit(RLIMIT_AS, &rl);
  rl.rlim_cur = rl.rlim_max = 900;
  setrlimit(RLIMIT_CPU, &rl);
  rl.rlim_cur = rl.rlim_max = 64ULL << 20;
  setrlimit(RLIMIT_FSIZE, &rl);
  nice(5);
}

}  // namespace arli
