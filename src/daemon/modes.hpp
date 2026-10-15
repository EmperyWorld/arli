#pragma once

#include <string>

namespace arli {
namespace modes {

// Inventory turn: list/glob/summarize only — no read_file or writes.
bool turn_is_list_only(const std::string& user_text);

bool tool_allowed_in_list_only(const std::string& tool_name);

const char* list_only_block_message();

}  // namespace modes
}  // namespace arli
