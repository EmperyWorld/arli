#pragma once
#include "mock_scenarios.hpp"

namespace mock_recovery {
using mock_scenarios::call;
using mock_scenarios::done;
using mock_scenarios::json;
inline std::optional<json> reply(const std::string& model, const json& messages) {
  if (model.rfind("recovery_", 0) != 0) return std::nullopt;
  int count = 0;
  for (const auto& message : messages) count += message.value("role", "") == "tool";
  if (model.rfind("recovery_code_", 0) == 0) {
    if (count == 0) return call("read_file", {{"path", "module.js"}});
    if (count == 1) return call("task_stage", {{"title", "Code edit recovery"}, {"checks", {"sh -n verify.sh"}}, {"remaining", {"Explorer"}}});
    if (count == 2) return call("replace_text", {{"path", "module.js"}, {"old_text", "value = 1"}, {"new_text", "value = 2"}});
    if (count == 3 && model == "recovery_code_failed") return call("run_shell", {{"command", "sh -n verify.sh"}});
    return done("FALSE_SUCCESS: Code and checks complete.");
  }
  if (model == "recovery_shell_invalidates") {
    if (count == 0) return call("task_stage", {{"title", "Shell recovery"}, {"checks", {"sh ./verify.sh"}}, {"remaining", {"Explorer"}}});
    if (count == 1) return call("run_shell", {{"command", "sh ./verify.sh"}});
    if (count == 2) return call("run_shell", {{"command", "sh ./mutate.sh"}});
    return done("FALSE_SUCCESS: Earlier passed check is still enough.");
  }
  const json path = {{"path", "config.json"}};
  if (model == "recovery_check_only") {
    if (count == 0) return call("check_file", path);
    return done("FALSE_SUCCESS: Я всё исправила, проверка проходит.");
  }
  if (count == 0) return call("read_file", path);
  if (model == "recovery_fix") {
    if (count == 1) return call("replace_text", {{"path", "config.json"}, {"old_text", "\"value\":1"}, {"new_text", "\"value\":2"}});
    if (count == 2) return call("check_file", path);
    return done("Corrected target, syntax checked.");
  }
  if (count == 1) return call("replace_text", {{"path", "config.json"}, {"old_text", "\"value\":1"}, {"new_text", "\"value\":"}});
  if (count == 2) return call("check_file", path);
  if (model == "recovery_unrelated" || model == "recovery_abandon") {
    if (count == 3) return call("replace_text", {{"path", "config.json"}, {"old_text", "\"label\":\"old\""}, {"new_text", "\"label\":\"new\""}});
    if (count == 4) return call("check_file", path);
    if (count == 5 && model == "recovery_abandon")
      return call("abandon_edit", {{"path", "config.json"}, {"reason", "Изменение value остаётся невыполненным"}});
  }
  return done("FALSE_SUCCESS: Я всё исправила, проверка проходит.");
}
}
