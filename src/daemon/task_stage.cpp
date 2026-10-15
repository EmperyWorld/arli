#include "task_stage.hpp"
#include "turn_policy.hpp"
#include "redact.hpp"
#include "shell_policy.hpp"
#include <algorithm>
#include <sstream>

namespace arli {
using json = nlohmann::json;
namespace {
constexpr std::size_t state_limit = 16000;
bool text(const json& value, std::size_t limit) {
  return value.is_string() && value.get_ref<const std::string&>().size() <= limit &&
      value.get_ref<const std::string&>().find_first_not_of(" \t\r\n") != std::string::npos &&
      value.get_ref<const std::string&>().find('\0') == std::string::npos;
}
bool valid(const json& a) {
  if (!a.is_object() || a.dump().size() > 12000 || !a.contains("title") || !a["title"].is_string() ||
      a["title"].get<std::string>().empty() || a["title"].get<std::string>().size() > 600 ||
      !a.contains("checks") || !a["checks"].is_array() || a["checks"].empty() || a["checks"].size() > 4 ||
      !a.contains("remaining") || !a["remaining"].is_array() || a["remaining"].size() > 12) return false;
  if (a.contains("completed") && !a["completed"].is_boolean()) return false;
  for (const auto& check : a["checks"])
    if (!check.is_string() || check.get<std::string>().empty() || check.get<std::string>().size() > 4000 ||
        check.get<std::string>().find('\0') != std::string::npos) return false;
  for (const auto& item : a["remaining"])
    if (!item.is_string() || item.get<std::string>().size() > 600) return false;
  return true;
}
bool same_stage(const json& a, const json& b) {
  return a.contains("title") && b.contains("title") && a["title"] == b["title"] && a["checks"] == b["checks"];
}
void merge_remaining(json& into, const json& from) {
  for (const auto& item : from["remaining"])
    if (std::find(into["remaining"].begin(), into["remaining"].end(), item) == into["remaining"].end())
      into["remaining"].push_back(item);
}
bool code_path(const std::string& path) {
  static const std::set<std::string> extensions = {".js", ".mjs", ".cjs", ".ts", ".tsx", ".jsx", ".py", ".cpp", ".hpp", ".cc", ".h", ".rs", ".go", ".qml"};
  return extensions.contains(std::filesystem::path(path).extension().string());
}
}
TaskStage::TaskStage(const json& saved) {
  if (!saved.is_object() || saved.dump().size() > state_limit) return;
  auto current = saved; current.erase("deferred");
  if (!current.empty() && !valid(current)) return;
  if (saved.contains("deferred")) {
    if (!saved["deferred"].is_array() || saved["deferred"].size() > 12) return;
    for (auto item : saved["deferred"]) {
      if (!item.contains("reason") || !text(item["reason"], 512)) return;
      item.erase("reason");
      if (!valid(item)) return;
    }
  }
  state_ = saved;
  // Even after a successful previous turn, checks are never restored as passed.
}
json TaskStage::schemas() {
  return json::parse(R"json([
    {"type":"function","function":{"name":"task_stage","description":"After examining the project, record a bounded milestone and 1-4 exact foreground verification commands. Run these via run_shell after the final edit. Prefer behavioral assertions/tests over syntax-only checks. remaining lists work outside this milestone. An unfinished stage cannot be replaced; it survives Continue. User requirements remain in force.","parameters":{"type":"object","properties":{"title":{"type":"string"},"checks":{"type":"array","items":{"type":"string"}},"remaining":{"type":"array","items":{"type":"string"}}},"required":["title","checks","remaining"]}}},
    {"type":"function","function":{"name":"abandon_edit","description":"Explicitly leave rejected edits to this path unfinished, with an honest reason. Does not change files or claim the requested work was completed.","parameters":{"type":"object","properties":{"path":{"type":"string"},"reason":{"type":"string"}},"required":["path","reason"]}}},
    {"type":"function","function":{"name":"defer_task_stage","description":"Explicitly defer the current milestone when its checks are unavailable or the user changes scope. Provide the reason. Obligations remain saved and the overall result remains partial; this does not satisfy the checks. Then task_stage may record a new bounded milestone. Resume a deferred milestone using the same title and checks.","parameters":{"type":"object","properties":{"reason":{"type":"string"}},"required":["reason"]}}}
  ])json");
}
ToolResult TaskStage::set(const json& args) {
  if (!valid(args)) return {false, "task_stage: нужны title (до 600 байт), checks (1–4 команды) и remaining (до 12 пунктов); весь план до 12 KiB"};
  // Arli tool names are not executables. Reject this common planning error
  // before it becomes a durable obligation which no shell command can satisfy.
  static const std::set<std::string> tool_names = {"check_file", "read_file", "replace_text", "edit_file", "append_file",
      "create_file", "run_shell", "task_stage", "defer_task_stage", "abandon_edit"};
  for (const auto& command : args["checks"]) {
    if (shell_policy::masks_failure_status(command.get<std::string>()))
      return {false, "STAGE_CHECK_STATUS: команда скрывает код завершения теста. Убери echo $?, '; true' или '|| true'; run_shell сам сообщает exit code."};
    for (const auto& segment : shell_policy::segments(command.get<std::string>())) {
    const auto words = shell_policy::tokens(segment);
    if (!words.empty() && tool_names.contains(words.front()))
      return {false, "STAGE_CHECK_COMMAND: " + words.front() + " — инструмент Arli, а не программа shell. "
          "Вызывай check_file отдельно. В checks нужны команды тестов поведения, например node tests/fs.test.cjs "
          "(создай тест с assertions, если его нет и создание разрешено). Затем выполни эту же команду через run_shell. Подэтап не сохранён."};
    }
  }
  json clean = {{"title", args["title"]}, {"checks", args["checks"]}, {"remaining", args["remaining"]}};
  if (active() && !passed() && !state_.value("completed", false)) {
    if (clean["title"] != state_["title"] || clean["checks"] != state_["checks"])
      return {false, "STAGE_PENDING: текущий подэтап ещё не проверен. Выполни проверки либо явно отложи его через defer_task_stage с причиной; обязательства сохранятся."};
  }
  const bool same = same_stage(clean, state_);
  if (same) merge_remaining(clean, state_);
  const auto deferred = state_.value("deferred", json::array());
  for (const auto& item : deferred) if (same_stage(clean, item)) merge_remaining(clean, item);
  if (!valid(clean)) return {false, "STAGE_LIMIT: объединённый список remaining или план превышает лимит; обязательства не удалены"};
  clean["completed"] = same && passed();
  if (!deferred.empty()) clean["deferred"] = deferred;
  if (clean.dump().size() > state_limit) return {false, "STAGE_LIMIT: сохранённые подэтапы превышают 16 KiB; обязательства не удалены"};
  state_ = json::parse(redact::text(clean.dump()));
  if (same) return {true, "Подэтап и remaining сохранены. Проверки: " + state_["checks"].dump()};
  passed_commands_.clear();
  return {true, "Подэтап сохранён; после последней правки выполни через run_shell: " + state_["checks"].dump()};
}
ToolResult TaskStage::defer(const json& args) {
  if (!active()) return {false, "STAGE_MISSING: нет текущего подэтапа для переноса"};
  if (!args.is_object() || !args.contains("reason") || !text(args["reason"], 512))
    return {false, "defer_task_stage: нужна непустая причина до 512 байт без NUL"};
  auto deferred = state_.value("deferred", json::array());
  auto item = state_; item.erase("deferred"); item["completed"] = false; item["reason"] = args["reason"];
  bool found = false;
  for (auto& previous : deferred) if (same_stage(previous, item)) { previous = item; found = true; break; }
  if (!found) deferred.push_back(item);
  json next = {{"deferred", deferred}};
  if (deferred.size() > 12 || next.dump().size() > state_limit)
    return {false, "STAGE_LIMIT: архив обязательств заполнен; текущий подэтап сохранён"};
  state_ = json::parse(redact::text(next.dump()));
  passed_commands_.clear();
  return {true, "Подэтап явно отложен и сохранён с причиной; результат остаётся частичным. Можно выбрать новый подэтап."};
}
void TaskStage::observe(const std::string& tool, const json& args, const ToolResult& result, bool foreground) {
  bool evidence_changed = false;
  if (result.ok && tool_writes_workdir(tool)) {
    auto path = coerce_path_arg(args);
    if (!path.empty()) { changed_.insert(path); code_changed_ = code_changed_ || code_path(path); }
    passed_commands_.clear();
    evidence_changed = true;
  }
  if (tool == "start_background") { passed_commands_.clear(); evidence_changed = true; }
  if (active() && tool == "run_shell" && foreground) {
    evidence_changed = true;
    const auto command = args.is_object() && args.contains("command") && args["command"].is_string()
        ? args["command"].get<std::string>() : std::string();
    if (std::find(state_["checks"].begin(), state_["checks"].end(), json(command)) != state_["checks"].end()) {
      if (result.ok) passed_commands_.insert(command); else passed_commands_.erase(command);
    } else passed_commands_.clear(); // An unrelated command can mutate even when it exits unsuccessfully.
  }
  if (active() && evidence_changed) {
    state_["completed"] = passed();
    if (passed() && has_deferred()) {
      auto& deferred = state_["deferred"];
      for (auto it = deferred.begin(); it != deferred.end();)
        if (same_stage(*it, state_)) it = deferred.erase(it); else ++it;
      if (deferred.empty()) state_.erase("deferred");
    }
  }
}
bool TaskStage::passed() const {
  if (!active()) return false;
  for (const auto& cmd : state_["checks"]) if (!passed_commands_.contains(cmd.get<std::string>())) return false;
  return true;
}
std::string TaskStage::pending() const {
  if (!active()) return {};
  for (const auto& cmd : state_["checks"]) if (!passed_commands_.contains(cmd.get<std::string>()))
    return "Не выполнена проверка подэтапа после последней правки: " + cmd.get<std::string>();
  return {};
}
json TaskStage::context() const {
  return {{"stage", state_}, {"changed_this_turn", changed_}, {"passed_commands_this_turn", passed_commands_}};
}
std::string TaskStage::final_text(const std::string& pending_edits, bool unverified, bool abandoned) const {
  std::ostringstream out;
  if (pending_edits.empty() && !abandoned && !has_deferred() && passed()) out << "Команды проверки выбранного подэтапа прошли.\n";
  else if (!active() && pending_edits.empty() && !abandoned && !has_deferred() && !unverified) out << "Заданные критерии результата проверены.\n";
  else out << "Результат подтверждён частично; полное выполнение задачи не подтверждено.\n";
  if (passed() && unverified) out << "Эти проверки не подтверждают завершение всей исходной задачи.\n";
  if (active()) out << "Подэтап: " << state_["title"].get<std::string>() << "\n";
  if (!changed_.empty()) { out << "Записаны изменения: "; for (const auto& p : changed_) out << p << "; "; out << '\n'; }
  if (passed_commands_.empty()) out << "Команды проверки подэтапа не выполнены.\n";
  else { out << "Успешные команды проверки после последней правки:\n"; for (const auto& cmd : passed_commands_) out << cmd << '\n'; }
  if (!pending_edits.empty()) out << "Не завершено: " << pending_edits << '\n';
  if (abandoned) out << "Есть явно отложенные правки; они не считаются выполненными.\n";
  if (!pending().empty()) out << pending() << '\n';
  if (active() && !state_["remaining"].empty()) { out << "Осталось за пределами подэтапа:\n"; for (const auto& item : state_["remaining"]) out << "- " << item.get<std::string>() << '\n'; }
  if (has_deferred()) {
    out << "Явно отложенные подэтапы (не выполнены):\n";
    for (const auto& item : state_["deferred"]) out << "- " << item["title"].get<std::string>() << ": " << item["reason"].get<std::string>() << '\n';
  }
  return redact::text(out.str());
}
}
