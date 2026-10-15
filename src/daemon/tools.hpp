#pragma once

#include "tools/shell_lex.hpp"
#include "tools/shell_run.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace arli {

struct ReadCoverage {int first=0,last=0,total=-1;std::string hash;};
struct ToolResult {
  bool ok = false;
  std::string output;
  std::string diff;  // unified diff for UI (Observation.diff); not sent to the model as tool content
  ReadCoverage read;
};

std::filesystem::path resolve_under(const std::filesystem::path& workdir, const std::string& rel);

ToolResult tool_list_dir(const std::filesystem::path& workdir, const std::string& path,
                          const std::string& list_ignore = {});
// Numbered window: "12|text". start_line 0 → from line 1, default 200 lines.
ToolResult tool_read_file(const std::filesystem::path& workdir, const std::string& path, int max_bytes = 120000,
                          int start_line = 0, int end_line = 0);

// Replace inclusive 1-based line range. new_text may contain newlines.
ToolResult tool_edit_lines(const std::filesystem::path& workdir, const std::string& path, int start_line,
                           int end_line, const std::string& new_text, const std::string* expected = nullptr);

// New file only. Existing path → FAIL (use edit_file / append_file).
ToolResult tool_create_file(const std::filesystem::path& workdir, const std::string& path, const std::string& content);

// Append to existing file (creates parent dirs). Missing file → FAIL.
ToolResult tool_append_file(const std::filesystem::path& workdir, const std::string& path, const std::string& content,
                            const std::string* expected = nullptr);

// Search/replace on existing file: unique match, LF+trailing-ws fuzzy, atomic write.
ToolResult tool_edit_file(const std::filesystem::path& workdir, const std::string& path, const std::string& old_text,
                          const std::string& new_text, const std::string* expected = nullptr);

ToolResult tool_rg_search(const std::filesystem::path& workdir, const std::string& pattern, const std::string& path,
                           const std::string& list_ignore = {});

// Parse tool args; repairs truncated JSON (missing }/]); coalesces path aliases.
// Returns false + error in out_err on hard failure.
bool parse_tool_args(const std::string& args_json, nlohmann::json& out, std::string& out_err);

// path | file | file_path | filePath | filepath | filename | target
std::string coerce_path_arg(const nlohmann::json& args);

// Gemma often omits path on edit_file. Infer from recent read_file buffers when old_text matches.
// Returns empty if path already set or cannot uniquely infer.
std::string infer_path_from_reads(const nlohmann::json& args,
                                  const std::vector<std::pair<std::string, std::string>>& recent_reads);

// Inject "path" into args JSON string (reparses/repairs). Returns updated JSON text.
std::string args_with_path(const std::string& args_json, const std::string& path);

ToolResult dispatch_tool(const std::filesystem::path& workdir, const std::string& name, const std::string& args_json,
                         const std::string& list_ignore = {}, bool read_only = false);

std::string tools_openai_schema_json();

}  // namespace arli
