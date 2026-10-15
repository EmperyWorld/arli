#include "registry.hpp"

#include "turn_policy.hpp"

#include "git_tools.hpp"
#include "http_tools.hpp"
#include "tier_a.hpp"
#include "syntax_check.hpp"

namespace arli::tools_registry {
namespace {

using json = nlohmann::json;

std::string require_path(const json& args, const char* tool) {
  if (!args.contains("path") || !args["path"].is_string() || args["path"].get<std::string>().empty()) {
    return std::string(tool) + ": path required";
  }
  return {};
}

std::string coerce_path(const json& args) {
  return args.value("path", "");
}

std::string coerce_str(const json& args, std::initializer_list<const char*> keys) {
  for (const char* k : keys) {
    if (args.contains(k) && args[k].is_string()) {
      return args[k].get<std::string>();
    }
  }
  return {};
}

int coerce_int(const json& args, const char* key, int fallback) {
  if (args.contains(key) && args[key].is_number_integer()) {
    return args[key].get<int>();
  }
  return fallback;
}

}  // namespace

std::optional<ToolResult> dispatch(const std::filesystem::path& workdir, const std::string& name,
                                   const json& args, const std::string& list_ignore, bool read_only) {
  if (read_only && (tool_writes_workdir(name) || name == "http_request" || name == "http_get")) {
    return ToolResult{false, "read_only: изменяющий инструмент отключён"};
  }
  if (name == "check_file") {
    if (auto e = require_path(args, "check_file"); !e.empty()) return ToolResult{false, e};
    return tool_check_file(workdir, coerce_path(args));
  }
  if (name == "glob_files") {
    return tool_glob_files(workdir, coerce_str(args, {"pattern"}), coerce_path(args), list_ignore);
  }
  if (name == "file_stat") {
    if (auto e = require_path(args, "file_stat"); !e.empty()) {
      return ToolResult{false, e};
    }
    return tool_file_stat(workdir, coerce_path(args));
  }
  if (name == "search_files") {
    return tool_search_files(workdir, coerce_str(args, {"name", "query", "substring"}), coerce_path(args),
                             list_ignore);
  }
  if (name == "summarize_dir") {
    return tool_summarize_dir(workdir, coerce_path(args).empty() ? "." : coerce_path(args), list_ignore);
  }
  if (name == "quote_file") {
    if (auto e = require_path(args, "quote_file"); !e.empty()) {
      return ToolResult{false, e};
    }
    return tool_quote_file(workdir, coerce_path(args), coerce_int(args, "start_line", 1),
                           coerce_int(args, "end_line", 0));
  }
  if (name == "make_dir") {
    if (auto e = require_path(args, "make_dir"); !e.empty()) {
      return ToolResult{false, e};
    }
    return tool_make_dir(workdir, coerce_path(args));
  }
  if (name == "copy_file") {
    const std::string src = coerce_str(args, {"src", "source", "from"});
    const std::string dest = coerce_str(args, {"dest", "destination", "to"});
    if (src.empty() || dest.empty()) {
      return ToolResult{false, "copy_file: src and dest required"};
    }
    return tool_copy_file(workdir, src, dest);
  }
  if (name == "move_file") {
    const std::string src = coerce_str(args, {"src", "source", "from"});
    const std::string dest = coerce_str(args, {"dest", "destination", "to"});
    if (src.empty() || dest.empty()) {
      return ToolResult{false, "move_file: src and dest required"};
    }
    return tool_move_file(workdir, src, dest);
  }
  if (name == "delete_file" || name == "delete_dir") {
    return ToolResult{false, name + " requires confirm gate in turn loop"};
  }
  if (name == "restore_file") {
    if (auto e = require_path(args, "restore_file"); !e.empty()) {
      return ToolResult{false, e};
    }
    return tool_restore_file(workdir, coerce_path(args));
  }
  if (name == "git_status") {
    return tool_git_status(workdir);
  }
  if (name == "git_diff") {
    return tool_git_diff(workdir, coerce_path(args));
  }
  if (name == "git_log") {
    return tool_git_log(workdir, coerce_int(args, "max_count", 20));
  }
  if (name == "http_get") {
    const std::string url = coerce_str(args, {"url"});
    if (url.empty()) {
      return ToolResult{false, "http_get: url required"};
    }
    return tool_http_get(url);
  }
  if (name == "http_request") {
    const std::string url = coerce_str(args, {"url"});
    if (url.empty()) {
      return ToolResult{false, "http_request: url required"};
    }
    return tool_http_request(coerce_str(args, {"method", "verb"}), url, coerce_str(args, {"body", "data"}));
  }
  return std::nullopt;
}

void append_openai_schemas(json& tools) {
  auto add = [&](const char* name, const char* desc, json params) {
    tools.push_back({{"type", "function"},
                     {"function", {{"name", name}, {"description", desc}, {"parameters", std::move(params)}}}});
  };
  add("glob_files", "Glob files under path by filename pattern (fnmatch).",
      {{"type", "object"},
       {"properties", {{"pattern", {{"type", "string"}}}, {"path", {{"type", "string"}}}}},
       {"required", {"pattern"}}});
  add("check_file", "Parse a JS, MJS, CJS, Python or JSON file without executing it. Reports syntax errors; does not test behavior.",
      {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}, {"required", {"path"}}});
  add("file_stat", "File or directory metadata (size, mtime, approximate lines).",
      {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}, {"required", {"path"}}});
  add("search_files", "Find paths whose filename contains a substring.",
      {{"type", "object"},
       {"properties", {{"name", {{"type", "string"}}}, {"path", {{"type", "string"}}}}},
       {"required", {"name"}}});
  add("summarize_dir", "Summarize a directory: file counts by extension and total bytes.",
      {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}, {"required", json::array()}});
  add("quote_file", "Read-only line range for citations (like read_file window).",
      {{"type", "object"},
       {"properties",
        {{"path", {{"type", "string"}}},
         {"start_line", {{"type", "integer"}}},
         {"end_line", {{"type", "integer"}}}}},
       {"required", {"path", "start_line"}}});
  add("make_dir", "Create a directory under workdir (parents ok).",
      {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}, {"required", {"path"}}});
  add("copy_file", "Copy file within workdir; backs up existing dest.",
      {{"type", "object"},
       {"properties", {{"src", {{"type", "string"}}}, {"dest", {{"type", "string"}}}}},
       {"required", {"src", "dest"}}});
  add("move_file", "Move/rename file within workdir; backs up existing dest.",
      {{"type", "object"},
       {"properties", {{"src", {{"type", "string"}}}, {"dest", {{"type", "string"}}}}},
       {"required", {"src", "dest"}}});
  add("delete_file", "Delete a file (UI confirm required). Creates a verified protected backup first.",
      {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}, {"required", {"path"}}});
  add("delete_dir", "Delete a bounded regular directory tree (UI confirm required). Symlinks/special files are refused; files are tracked for revert.",
      {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}, {"required", {"path"}}});
  add("restore_file", "Restore a file from its protected Arli backup.",
      {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}, {"required", {"path"}}});
  add("git_status", "Git status --short in workdir (read-only).",
      {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}});
  add("git_diff", "Git diff (optional path).",
      {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}, {"required", json::array()}});
  add("git_log", "Git log --oneline.",
      {{"type", "object"}, {"properties", {{"max_count", {{"type", "integer"}}}}}, {"required", json::array()}});
  add("http_get", "HTTP GET with SSRF guard (no private IPs). Max 1 MiB.",
      {{"type", "object"}, {"properties", {{"url", {{"type", "string"}}}}}, {"required", {"url"}}});
  add("http_request", "HTTP request with SSRF guard.",
      {{"type", "object"},
       {"properties",
        {{"method", {{"type", "string"}}}, {"url", {{"type", "string"}}}, {"body", {{"type", "string"}}}}},
       {"required", {"url"}}});
}

}  // namespace arli::tools_registry
