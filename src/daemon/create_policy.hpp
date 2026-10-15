#pragma once
#include "tools.hpp"
#include "turn_policy.hpp"

namespace arli {
// Recognizes explicit creation commands and scoped exclusions; ambiguity stays denied.
void classify_creation_policy(TurnIntent& intent, const std::string& original, const std::string& lower);
ToolResult creation_gate(const TurnIntent& intent, const std::filesystem::path& workdir,
                         const std::string& tool, const std::string& args,bool readonly_shell=false);
}  // namespace arli
