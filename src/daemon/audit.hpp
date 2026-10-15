#pragma once

#include "db.hpp"

#include "arli.grpc.pb.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace arli::audit {

// Append-only per-task audit in state_root()/audit/{task_id}/ (outside the workdir):
//   task.json    — meta: ids, user text, status, timestamps, model (never the API key)
//   events.jsonl — one JSON line per relayed event; outputs truncated to kMaxOutput
inline constexpr std::size_t kMaxOutput = 4096;

std::filesystem::path task_dir(std::int64_t task_id);

// Called when the task is claimed (creates the directory, writes task.json with status=running).
void open_task(const TaskRow& task, const std::string& worker_id, const std::string& llm_model);
// One relayed event (TurnEvent payload) → one line.
void append_event(std::int64_t task_id, const StepRow& step);
// Terminal status → task.json rewritten with status/error/finished_at.
void close_task(const TaskRow& task);

// Step → JSON (shared with the log sink); secrets are never part of a StepRow.
nlohmann::json step_json(const StepRow& step);

}  // namespace arli::audit
