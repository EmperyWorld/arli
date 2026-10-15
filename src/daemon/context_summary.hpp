#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace arli {
namespace context {

// Deterministic one-block summary of messages[from, to) for compact().
std::string build_fold_summary(const nlohmann::json& messages, int from, int to);

}  // namespace context
}  // namespace arli
