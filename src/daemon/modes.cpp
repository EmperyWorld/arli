#include "modes.hpp"

#include "turn_policy.hpp"

namespace arli {
namespace modes {

bool turn_is_list_only(const std::string& user_text) {
  if (wants_file_work(user_text)) {
    return false;
  }
  if (wants_single_file_read(user_text)) {
    return false;
  }
  return wants_inventory_list(user_text);
}

bool tool_allowed_in_list_only(const std::string& tool_name) {
  return tool_name == "list_dir" || tool_name == "glob_files" || tool_name == "search_files" ||
         tool_name == "summarize_dir" || tool_name == "file_stat" || tool_name == "git_status" ||
         tool_name == "git_diff" || tool_name == "git_log" || tool_name == "ask_user" ||
         tool_name == "file_pass_set" || tool_name == "file_pass_clear" || tool_name == "file_pass_review" || tool_name == "plan_set" ||
         tool_name == "plan_advance" || tool_name == "plan_fail" || tool_name == "plan_retry" ||
         tool_name == "plan_edit" || tool_name == "remember" || tool_name == "forget" ||
         tool_name == "list_memory";
}

const char* list_only_block_message() {
  return "этот ход только списки. Файл не открыт и не записан";
}

}  // namespace modes
}  // namespace arli
