#pragma once

#include <string>
#include <vector>

namespace arli {

struct TurnIntent {
  bool requires_tools = false;
  bool expects_mutation = false;
  bool needs_clarification = false;
  bool forbid_mutation = false;
  bool forbid_create = false;
  std::vector<std::string> allowed_create_paths;
};

// Conservative intent hints for retries and explicit user restrictions; no write quota.
TurnIntent classify_turn_intent(const std::string& user_text, const std::string& previous_answer = {});

// User explicitly asks to change files or the project.
bool wants_file_work(const std::string& user_text);

// User wants to open/read one file (not list-only inventory).
bool wants_single_file_read(const std::string& user_text);

// User wants a directory/file inventory without opening a single file.
bool wants_inventory_list(const std::string& user_text);

bool is_mutation_tool(const std::string& tool_name);

// Writes/deletes under workdir (blocked in read_only mode).
bool tool_writes_workdir(const std::string& tool_name);

// Tools that only look around (no disk growth).
bool is_explore_tool(const std::string& tool_name);

// Stable key for thrash detection: tool + all canonical JSON arguments.
std::string tool_call_key(const std::string& tool_name, const std::string& args_json);

}  // namespace arli
