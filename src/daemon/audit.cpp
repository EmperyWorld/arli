#include "audit.hpp"
#include "redact.hpp"

#include "log.hpp"
#include "paths.hpp"
#include "context.hpp"

#include <fstream>
#include <mutex>
#include <system_error>

namespace arli::audit {
namespace {

std::mutex g_mu;

std::string truncate(const std::string& raw) {
  const auto s = redact::text(raw);
  if (s.size() <= kMaxOutput) {
    return s;
  }
  const auto cut = arli::context::utf8_floor(s, kMaxOutput);
  return s.substr(0, cut) + "…[" + std::to_string(s.size() - cut) + " bytes cut]";
}

void sanitize(nlohmann::json& value) {
  if(value.is_string())value=redact::text(value.get<std::string>());
  else if(value.is_structured())for(auto& child:value)sanitize(child);
}

void write_task_json(const std::filesystem::path& dir, const nlohmann::json& meta) {
  const auto tmp = dir / "task.json.tmp";
  {
    std::ofstream f(tmp, std::ios::trunc);
    if (!f) {
      return;
    }
    auto safe=meta;sanitize(safe);
    f << safe.dump(2) << '\n';
  }
  std::error_code ec;
  std::filesystem::rename(tmp, dir / "task.json", ec);
}

nlohmann::json read_task_json(const std::filesystem::path& dir) {
  std::ifstream f(dir / "task.json");
  if (!f) {
    return nlohmann::json::object();
  }
  try {
    return nlohmann::json::parse(f);
  } catch (...) {
    return nlohmann::json::object();
  }
}

}  // namespace

std::filesystem::path task_dir(std::int64_t task_id) {
  return state_root() / "audit" / std::to_string(task_id);
}

nlohmann::json step_json(const StepRow& step) {
  nlohmann::json j = {
      {"ts", step.created_at}, {"dialog_id", step.dialog_id}, {"turn_id", step.turn_id},
      {"task_id", step.task_id}, {"kind", step.kind},        {"ok", step.ok},
  };
  if (step.step_id) {
    j["step_id"] = step.step_id;
  }
  if (!step.tool.empty()) {
    j["tool"] = step.tool;
  }
  if (!step.args_json.empty()) {
    j["args"] = truncate(step.args_json);
  }
  if (!step.output.empty()) {
    j["output"] = truncate(step.output);
  }
  if (step.duration_ms) {
    j["duration_ms"] = step.duration_ms;
  }
  sanitize(j);
  return j;
}

void open_task(const TaskRow& task, const std::string& worker_id, const std::string& llm_model) {
  std::lock_guard lock(g_mu);
  try {
    const auto dir = task_dir(task.id);
    std::filesystem::create_directories(dir);
    nlohmann::json meta = {
        {"task_id", task.id},         {"dialog_id", task.dialog_id}, {"turn_id", task.turn_id},
        {"user_text", truncate(task.text)}, {"created_at", task.created_at}, {"claimed_at", task.updated_at},
        {"status", "running"},        {"worker_id", worker_id},      {"llm_model", llm_model},
    };
    write_task_json(dir, meta);
  } catch (const std::exception& e) {
    log::warn("audit", std::string("open_task failed: ") + e.what());
  }
}

void append_event(std::int64_t task_id, const StepRow& step) {
  std::lock_guard lock(g_mu);
  try {
    const auto dir = task_dir(task_id);
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "events.jsonl", std::ios::app);
    if (f) {
      f << step_json(step).dump() << '\n';
    }
  } catch (const std::exception& e) {
    log::warn("audit", std::string("append_event failed: ") + e.what());
  }
}

void close_task(const TaskRow& task) {
  std::lock_guard lock(g_mu);
  try {
    const auto dir = task_dir(task.id);
    if (!std::filesystem::exists(dir)) {
      return;  // never claimed (cancelled in queue / failed before dispatch)
    }
    nlohmann::json meta = read_task_json(dir);
    meta["status"] = task.status;
    meta["error"] = task.error;
    meta["finished_at"] = task.finished_at;
    write_task_json(dir, meta);
  } catch (const std::exception& e) {
    log::warn("audit", std::string("close_task failed: ") + e.what());
  }
}

}  // namespace arli::audit
