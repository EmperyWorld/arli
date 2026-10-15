#pragma once

#include <cstdint>
#include "arli.pb.h"
#include <optional>
#include <string>
#include <vector>

namespace arli {

struct DialogRow {
  std::string instructions;
  std::int64_t id = 0;
  std::string title;
  std::string updated_at;
};

struct MessageRow {
  std::int64_t id = 0;
  std::int64_t dialog_id = 0;
  std::int64_t turn_id = 0;
  std::string role;
  std::string text;
  std::string created_at;
};

// One recorded event of a turn. Written by the orchestrator at the relay point.
// kind: thought | token | action | observation | confirm | ask_user | status | error | done
struct StepRow {
  std::int64_t id = 0;
  std::int64_t dialog_id = 0;
  std::int64_t turn_id = 0;
  std::int64_t task_id = 0;
  std::int64_t step_id = 0;  // worker's step counter (Action/Observation), 0 otherwise
  std::string kind;
  std::string tool;
  std::string args_json;
  bool ok = true;
  std::string output;  // text payload; truncated to kMaxStepOutput
  std::string diff;    // unified diff for mutation observations
  std::int64_t duration_ms = 0;
  std::string created_at;
};

inline constexpr std::size_t kMaxStepOutput = 64 * 1024;
inline constexpr std::size_t kMaxStepDiff = 32 * 1024;

struct SettingsRow {
  std::string llm_base_url = "http://127.0.0.1:8080/v1";
  std::string llm_model = "/models/gemma-4-26B.gguf";
  std::string llm_profile = "gemma4";
  double llm_temperature = -1.0;
  std::string llm_api_key;
  std::string workdir;
  // Lifecycle (seconds).
  int turn_timeout_sec = 2700;
  int idle_timeout_sec = 600;
  int cancel_grace_sec = 10;
  // Shell confirm policy: one token-prefix per line.
  std::string shell_auto;
  std::string shell_ask;
  // When true, commands skip the allow-list. The ask-list and delete toggle still apply.
  bool shell_allow_all = false;
  // rm / rmdir / unlink / find -delete / git clean ask even if every command is allowed.
  bool delete_confirm = true;
  // Directory names hidden from listings. Empty → built-in set (node_modules, .git, …).
  std::string list_ignore;
  int context_tokens = 16384;
  int max_steps = 64;
  int shell_timeout_sec = 120;
  bool shell_net = false;
  bool read_only = false;
  bool allow_unsandboxed_shell = false;
};

// A process the worker started in the background (dev server, npm start).
// The worker inserts the row; the orchestrator lists it and stops it.
struct JobRow {
  std::int64_t dialog_id = 0, task_id = 0, turn_id = 0;
  std::string finished_at;
  std::string process_identity;
  int exit_code = -1;
  std::int64_t id = 0;
  int pid = 0;
  std::string command;
  std::string workdir;
  std::string log_path;
  std::string status;
  std::string started_at;
};

// queued → running → done | failed | cancelled   (queued → cancelled before claim)
struct FilePassRow {
  std::vector<std::string> paths;
  std::vector<std::string> done;
  std::vector<std::string> reviewed;
  std::string updated_at;
};

struct PlanRow {
  std::vector<std::string> steps;
  int current = 0;
  bool failed = false;
  std::string updated_at;
};

struct MemoryRow {
  std::int64_t id = 0;
  std::string text;
  std::string created_at;
};

struct TaskRow {
  std::int64_t id = 0;
  std::int64_t dialog_id = 0;
  std::int64_t turn_id = 0;
  std::string text;
  std::string status;
  std::string worker_id;
  std::string error;
  std::string created_at;
  std::string updated_at;
  std::string finished_at;
  std::string config_json;
};

inline bool task_status_terminal(const std::string& s) {
  return s == "done" || s == "failed" || s == "cancelled";
}

class Database {
 public:
  explicit Database(std::string path);
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // Creates tables and applies migrations (idempotent; both processes call it).
  void init();
  int schema_version();
  std::int64_t create_command_receipt(std::int64_t task_id);
  void finish_command_receipt(std::int64_t command_id, const std::string& status);
  std::string command_receipt_status(std::int64_t command_id);

  SettingsRow get_settings();
  SettingsRow set_settings(const SettingsRow& in);

  std::vector<DialogRow> list_dialogs();
  std::vector<DialogRow> dialog_page(const std::string& token, int limit, std::string& next);
  DialogRow create_dialog(const std::string& title);
  bool set_dialog_instructions(std::int64_t id, const std::string& text);
  bool rename_dialog(std::int64_t id, const std::string& title);
  bool delete_dialog(std::int64_t id);
  std::optional<DialogRow> get_dialog(std::int64_t id);

  std::vector<MessageRow> list_messages(std::int64_t dialog_id, int limit = 200);
  std::vector<MessageRow> message_page(std::int64_t dialog_id, std::int64_t before, int limit, std::int64_t& next);
  MessageRow add_message(std::int64_t dialog_id, const std::string& role, const std::string& text,
                         std::int64_t turn_id = 0);
  // Worker-owned internal snapshot, excluded from chat history.
  std::string context_state(std::int64_t dialog_id);
  void save_context_state(std::int64_t dialog_id, const std::string& text);
  void touch_dialog(std::int64_t dialog_id);

  // ----- steps (owned by the orchestrator) -----
  std::int64_t add_step(const StepRow& row);
  std::vector<StepRow> list_steps_for_task(std::int64_t task_id);
  std::vector<StepRow> list_steps_for_turn(std::int64_t dialog_id, std::int64_t turn_id);
  std::vector<StepRow> list_steps_for_turn_page(std::int64_t dialog_id, std::int64_t turn_id,
                                                std::int64_t after_id, int limit, std::size_t byte_budget,
                                                bool& has_more);
  // Highest turn_id with recorded steps (0 if none).
  std::int64_t last_traced_turn(std::int64_t dialog_id);
  // Drop detailed relay/step payloads for old terminal tasks while retaining
  // task metadata and chat messages. Processes at most batch tasks per call.
  int retain_history(int keep_terminal = 5000, int max_age_days = 90, int batch = 128);

  // ----- tasks (owned by the orchestrator) -----
  TaskRow enqueue_task(std::int64_t dialog_id, std::int64_t turn_id, const std::string& text, const std::string& request_id = {}, const std::string& revert_changeset = {}, const TaskRow* revert_source = nullptr);
  std::optional<TaskRow> task_by_request(std::int64_t dialog_id, const std::string& request_id);
  bool valid_event_cursor(std::int64_t task_id, std::int64_t event_id);
  std::int64_t append_event(std::int64_t task_id, const v1::TurnEvent& event);
  std::vector<v1::TurnEvent> events_after(std::int64_t task_id, std::int64_t after, int limit = 64);
  std::optional<TaskRow> get_task(std::int64_t task_id);
  // CAS queued → running. False if the task is not queued any more.
  bool mark_running(std::int64_t task_id, const std::string& worker_id);
  // running → queued (worker rejected the claim).
  bool requeue_task(std::int64_t task_id);
  void touch_task(std::int64_t task_id);
  // Terminal transition; no-op if already terminal. Returns true if changed.
  bool finish_task(std::int64_t task_id, const std::string& status, const std::string& error = {}, v1::TurnEvent* final_event = nullptr);
  // All queued/running → failed(reason) (startup reconcile). Returns count.
  int fail_active_tasks(const std::string& reason);
  // Only running → failed(reason) (worker lost; queued tasks wait for the next worker).
  int fail_running_tasks(const std::string& reason);
  std::optional<TaskRow> next_queued();
  int count_queued();
  // Position of a queued task (1 = next). 0 if not queued.
  int queue_position(std::int64_t task_id);
  std::optional<TaskRow> active_task_for_dialog(std::int64_t dialog_id);
  std::optional<TaskRow> task_for_turn(std::int64_t dialog_id,std::int64_t turn_id);
  std::vector<TaskRow> list_tasks(std::int64_t dialog_id, int limit = 50);
  std::int64_t next_turn_id(std::int64_t dialog_id);

  // ----- background jobs (worker inserts, orchestrator stops) -----
  std::int64_t add_job(int pid, const std::string& command, const std::string& workdir,
                       const std::string& log_path, const std::string& process_identity = {},
                       std::int64_t dialog_id = 0, std::int64_t task_id = 0, std::int64_t turn_id = 0);
  std::vector<JobRow> list_jobs(int limit = 30, std::int64_t dialog_id = 0, std::int64_t task_id = 0);
  bool job_capacity_available(std::int64_t dialog_id = 0, std::int64_t task_id = 0);
  std::vector<JobRow> running_jobs_after(std::int64_t after, int limit = 64);
  std::vector<JobRow> expired_jobs_after(std::int64_t after, int limit = 32);
  bool forget_job(const JobRow& job);
  bool job_log_registered(const std::string& path);
  std::optional<JobRow> get_job(std::int64_t id);
  void finish_job(std::int64_t id, const std::string& status, int exit_code);
  void set_job_status(std::int64_t id, const std::string& status);

  std::optional<FilePassRow> get_file_pass(std::int64_t dialog_id);
  void set_file_pass(std::int64_t dialog_id, const std::vector<std::string>& paths,
                     const std::vector<std::string>& done, const std::vector<std::string>& reviewed = {});
  void clear_file_pass(std::int64_t dialog_id);

  std::optional<PlanRow> get_plan(std::int64_t dialog_id);
  void set_plan(std::int64_t dialog_id, const std::vector<std::string>& steps, int current, bool failed);
  void clear_plan(std::int64_t dialog_id);

  std::vector<MemoryRow> list_memory(int limit = 20);
  std::int64_t next_memory_id();
  void replace_memory(const std::vector<MemoryRow>& rows);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace arli
