#pragma once
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

namespace arli {
// Known host installation locations only; never PATH, a workspace executable or its symlink.
// Node is optional and is not bundled with Arli. Desktop services often lack nvm's PATH.
inline std::string host_interpreter(const std::filesystem::path& workdir, const std::string& name) {
  const auto root = std::filesystem::weakly_canonical(workdir);
  std::vector<std::filesystem::path> candidates{std::filesystem::path("/usr/bin") / name,
      std::filesystem::path("/bin") / name, std::filesystem::path("/usr/local/bin") / name};
  if (name == "node") if (const auto* home = std::getenv("HOME")) {
    const auto versions = std::filesystem::path(home) / ".nvm/versions/node";
    std::vector<std::pair<std::vector<unsigned long>, std::filesystem::path>> installed;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(versions, ec), end; !ec && it != end; it.increment(ec)) {
      const auto version = it->path().filename().string();
      if (version.size() < 6 || version.front() != 'v' || version.size() > 64 || version.back() == '.' ||
          version.find_first_not_of("0123456789.", 1) != std::string::npos) continue;
      std::vector<unsigned long> numbers;
      try {
        size_t pos = 1;
        while (pos < version.size()) {
          size_t used = 0; const auto n = std::stoul(version.substr(pos), &used);
          if (!used) break;
          numbers.push_back(n); pos += used;
          if (pos == version.size()) break;
          if (version[pos++] != '.') { numbers.clear(); break; }
        }
      } catch (...) { continue; }
      if (numbers.size() == 3) installed.emplace_back(std::move(numbers), it->path() / "bin/node");
    }
    std::sort(installed.begin(), installed.end(), [](const auto& a, const auto& b) {
      return a.first != b.first ? a.first > b.first : a.second > b.second;
    });
    for (const auto& entry : installed) candidates.push_back(entry.second);
  }
  for (const auto& candidate : candidates) {
    std::error_code ec;
    const auto file = std::filesystem::canonical(candidate, ec);
    if (ec) continue;
    const auto rel = file.lexically_relative(root);
    if (!rel.empty() && *rel.begin() != "..") continue;
    if (std::filesystem::is_regular_file(file, ec) && access(file.c_str(), X_OK) == 0) return file.string();
  }
  return {};
}
}
