#include "file_io.hpp"
#include <algorithm>
#include "tier_a.hpp"
#include "private_state.hpp"

#include "listing.hpp"

#include <fnmatch.h>

#include <map>
#include <sstream>

namespace arli {
namespace {

bool skip_part(const std::string& name) {
  return name == ".git" || name == ".arli-bak";
}

bool skip_rel(const std::filesystem::path& rel) {
  for (const auto& part : rel) {
    if (skip_part(part.string())) {
      return true;
    }
  }
  return false;
}

std::string rel_string(const std::filesystem::path& root, const std::filesystem::path& abs) {
  std::error_code ec;
  const auto rel = std::filesystem::relative(abs, root, ec);
  return ec ? abs.filename().string() : rel.generic_string();
}

bool fnmatch_name(const std::string& pattern, const std::string& name) {
  return fnmatch(pattern.c_str(), name.c_str(), 0) == 0;
}

}  // namespace

ToolResult tool_glob_files(const std::filesystem::path& workdir, const std::string& pattern,
                           const std::string& path, const std::string& list_ignore) {
  if (pattern.empty()) {
    return {false, "glob_files: pattern required"};
  }
  try {
    const auto root = resolve_under(workdir, path.empty() ? "." : path);
    if (!std::filesystem::is_directory(root)) {
      return {false, "not a directory: " + root.string()};
    }
    const auto skip = listing::names(list_ignore);
    std::ostringstream out;
    int count = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
      if (private_state_path(it->path())) { it.disable_recursion_pending(); continue; }
      const auto rel = rel_string(workdir, it->path());
      if (skip_rel(std::filesystem::path(rel))) {
        it.disable_recursion_pending();
        continue;
      }
      const std::string name = it->path().filename().string();
      if (listing::is_name(name, skip)) {
        continue;
      }
      if (!fnmatch_name(pattern, name) && !fnmatch_name(pattern, rel)) {
        continue;
      }
      if (count++) {
        out << '\n';
      }
      out << rel;
      if (count >= 2000) {
        out << "\n… truncated at 2000 paths";
        break;
      }
    }
    return {true, count ? out.str() : std::string("(no matches)")};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_file_stat(const std::filesystem::path& workdir, const std::string& path) {
  try {
    const auto file = resolve_under(workdir, path);
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
      return {false, "path not found: " + file.string()};
    }
    std::ostringstream out;
    if (std::filesystem::is_directory(file, ec)) {
      out << "type=dir\n";
    } else if (std::filesystem::is_regular_file(file, ec)) {
      const auto size=std::filesystem::file_size(file,ec);
      if(ec)return {false,"cannot stat file"};
      if(size<=64*1024*1024){
        const auto body=tools_internal::read_file_bytes(file);
        const auto lines=std::count(body.begin(),body.end(),'\n')+(!body.empty()&&body.back()!='\n');
        out << "type=file\nsize=" << body.size() << "\nlines=" << lines
            << "\nfinal_newline=" << (!body.empty()&&body.back()=='\n'?"yes":"no") << '\n';
      }else out << "type=file\nsize=" << size << "\nlines=unknown (over 64 MiB scan limit)\n";
    } else {
      out << "type=other\n";
    }
    const auto mtime = std::filesystem::last_write_time(file, ec);
    if (!ec) {
      out << "mtime=" << mtime.time_since_epoch().count() << '\n';
    }
    out << "path=" << rel_string(workdir, file);
    return {true, out.str()};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_search_files(const std::filesystem::path& workdir, const std::string& name_substring,
                             const std::string& path, const std::string& list_ignore) {
  if (name_substring.empty()) {
    return {false, "search_files: name substring required"};
  }
  try {
    const auto root = resolve_under(workdir, path.empty() ? "." : path);
    const auto skip = listing::names(list_ignore);
    std::ostringstream out;
    int count = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
      if (private_state_path(it->path())) { it.disable_recursion_pending(); continue; }
      const auto rel = rel_string(workdir, it->path());
      if (skip_rel(std::filesystem::path(rel))) {
        it.disable_recursion_pending();
        continue;
      }
      const std::string name = it->path().filename().string();
      if (listing::is_name(name, skip)) {
        continue;
      }
      if (name.find(name_substring) == std::string::npos) {
        continue;
      }
      if (count++) {
        out << '\n';
      }
      out << rel;
      if (count >= 2000) {
        out << "\n… truncated at 2000 paths";
        break;
      }
    }
    return {true, count ? out.str() : std::string("(no matches)")};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_summarize_dir(const std::filesystem::path& workdir, const std::string& path,
                              const std::string& list_ignore) {
  try {
    const auto root = resolve_under(workdir, path.empty() ? "." : path);
    if (!std::filesystem::is_directory(root)) {
      return {false, "not a directory"};
    }
    const auto skip = listing::names(list_ignore);
    std::map<std::string, int> ext_count;
    std::uintmax_t total = 0;
    int files = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
      if (private_state_path(it->path())) { it.disable_recursion_pending(); continue; }
      const auto rel = rel_string(workdir, it->path());
      if (skip_rel(std::filesystem::path(rel))) {
        it.disable_recursion_pending();
        continue;
      }
      if (!it->is_regular_file()) {
        continue;
      }
      const std::string name = it->path().filename().string();
      if (listing::is_name(name, skip)) {
        continue;
      }
      ++files;
      std::error_code ec;
      total += std::filesystem::file_size(it->path(), ec);
      const auto ext = it->path().extension().string();
      ext_count[ext.empty() ? "(no ext)" : ext]++;
    }
    std::ostringstream out;
    out << "files=" << files << " bytes=" << total << '\n';
    for (const auto& [ext, n] : ext_count) {
      out << ext << ": " << n << '\n';
    }
    return {true, out.str()};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_quote_file(const std::filesystem::path& workdir, const std::string& path, int start_line,
                           int end_line) {
  if (start_line < 1) {
    start_line = 1;
  }
  if (end_line < start_line) {
    end_line = start_line + 50;
  }
  return tool_read_file(workdir, path, 120000, start_line, end_line);
}

}  // namespace arli
