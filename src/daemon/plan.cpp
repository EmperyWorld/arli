#include "plan.hpp"

namespace arli {

PlanState PlanState::from_row(const PlanRow& row) {
  PlanState s;
  s.steps_ = row.steps;
  s.current_ = row.current;
  s.failed_ = row.failed;
  if (s.current_ < 0) {
    s.current_ = 0;
  }
  if (s.current_ >= static_cast<int>(s.steps_.size()) && !s.steps_.empty()) {
    s.current_ = static_cast<int>(s.steps_.size()) - 1;
  }
  return s;
}

ToolResult PlanState::set_steps(const nlohmann::json& args) {
  if (!args.contains("steps") || !args["steps"].is_array()) {
    return {false, "plan_set: нужен steps[]"};
  }
  std::vector<std::string> next;
  for (const auto& item : args["steps"]) {
    if (!item.is_string()) {
      continue;
    }
    std::string t = item.get<std::string>();
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) {
      t.erase(t.begin());
    }
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) {
      t.pop_back();
    }
    if (t.empty()) {
      continue;
    }
    if (t.size() > 400) {
      t.resize(400);
    }
    next.push_back(std::move(t));
    if (next.size() >= 64) {
      break;
    }
  }
  if (next.empty()) {
    return {false, "plan_set: steps пуст"};
  }
  steps_ = std::move(next);
  current_ = 0;
  failed_ = false;
  return {true, "план: " + std::to_string(steps_.size()) + " шагов, текущий — 1"};
}

ToolResult PlanState::advance(const FilePassState& pass) {
  if (const ToolResult gate = plan_gate_tool(pass, "plan_advance"); !gate.ok) {
    return gate;
  }
  if (steps_.empty()) {
    return {false, "plan_advance: план не задан (plan_set)"};
  }
  if (failed_) {
    return {false, "plan_advance: текущий шаг помечен сбоем — plan_retry или plan_edit"};
  }
  if (current_ + 1 >= static_cast<int>(steps_.size())) {
    return {true, "план завершён (" + std::to_string(steps_.size()) + " шагов)"};
  }
  ++current_;
  return {true, "шаг " + std::to_string(current_ + 1) + "/" + std::to_string(steps_.size()) + ": " +
                  steps_[static_cast<size_t>(current_)]};
}

ToolResult PlanState::mark_failed(const nlohmann::json& args) {
  if (steps_.empty()) {
    return {false, "plan_fail: план не задан"};
  }
  failed_ = true;
  std::string note = args.value("note", "");
  if (note.size() > 200) {
    note.resize(200);
  }
  std::string out = "шаг " + std::to_string(current_ + 1) + " — сбой";
  if (!note.empty()) {
    out += ": " + note;
  }
  return {true, out};
}

ToolResult PlanState::retry() {
  if (steps_.empty()) {
    return {false, "plan_retry: план не задан"};
  }
  if (!failed_) {
    return {true, "текущий шаг без сбоя — можно plan_advance"};
  }
  failed_ = false;
  return {true, "повтор шага " + std::to_string(current_ + 1) + ": " +
                  steps_[static_cast<size_t>(current_)]};
}

ToolResult PlanState::edit_step(const nlohmann::json& args) {
  if (steps_.empty()) {
    return {false, "plan_edit: план не задан"};
  }
  int index = current_;
  if (args.contains("index")) {
    if (!args["index"].is_number_integer()) {
      return {false, "plan_edit: index — целое число"};
    }
    index = args["index"].get<int>();
  }
  if (index < 0 || index >= static_cast<int>(steps_.size())) {
    return {false, "plan_edit: index вне плана"};
  }
  std::string text = args.value("text", "");
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.erase(text.begin());
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  if (text.empty()) {
    return {false, "plan_edit: нужен text"};
  }
  if (text.size() > 400) {
    text.resize(400);
  }
  steps_[static_cast<size_t>(index)] = std::move(text);
  if (index == current_) {
    failed_ = false;
  }
  return {true, "шаг " + std::to_string(index + 1) + " обновлён"};
}

void PlanState::fill_snapshot(v1::PlanSnapshot* out) const {
  out->clear_steps();
  for (const auto& s : steps_) {
    out->add_steps(s);
  }
  out->set_current(current_);
  out->set_current_failed(failed_);
}

void fill_turn_event_plan(const PlanState& plan, v1::TurnEvent& ev) {
  plan.fill_snapshot(ev.mutable_plan());
}

ToolResult plan_gate_tool(const FilePassState& pass, const std::string& tool) {
  if (tool != "plan_advance") {
    return {true, {}};
  }
  if (!pass.active() || !pass.hold_for_write()) {
    return {true, {}};
  }
  const std::string h = pass.head();
  return {false, "file_pass: заверши текущий файл правкой или file_pass_review (" + h + ") перед plan_advance"};
}

}  // namespace arli
