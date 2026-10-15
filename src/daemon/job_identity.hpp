#pragma once
#include <fstream>
#include <sstream>
#include <string>
namespace arli {
inline std::string process_identity(int pid) {
  if (pid <= 0) return {};
  std::ifstream boot("/proc/sys/kernel/random/boot_id"); std::string boot_id; std::getline(boot, boot_id);
  std::ifstream stat("/proc/" + std::to_string(pid) + "/stat"); std::string line; std::getline(stat, line);
  const auto end = line.rfind(')');
  if (boot_id.empty() || end == std::string::npos) return {};
  std::istringstream fields(line.substr(end + 2)); std::string value;
  for (int field = 3; field <= 22; ++field) {
    if (!(fields >> value) || (field == 3 && (value == "Z" || value == "X"))) return {};
  }
  return boot_id + ":" + value;
}
}
