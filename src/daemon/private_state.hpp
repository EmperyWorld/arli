#pragma once
#include "paths.hpp"
#include <filesystem>
#include <stdexcept>
namespace arli {
inline bool path_within(const std::filesystem::path& root,const std::filesystem::path& path) {
  const auto rel=path.lexically_relative(root);
  return !rel.empty() && *rel.begin()!="..";
}
inline bool private_state_path(const std::filesystem::path& path) {
  const auto state=std::filesystem::weakly_canonical(state_root());
  const auto resolved=std::filesystem::weakly_canonical(path);
  return path_within(state,resolved) && !path_within(state/"workspace",resolved);
}
inline void guard_private_state(const std::filesystem::path& path,bool recursive_change=false) {
  const auto resolved=std::filesystem::weakly_canonical(path);
  if(private_state_path(resolved) || (recursive_change && path_within(resolved,std::filesystem::weakly_canonical(state_root()))))
    throw std::runtime_error("PRIVATE_STATE: служебные файлы Arli недоступны инструментам");
}
}
