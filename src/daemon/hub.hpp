#pragma once

#include "db.hpp"

#include "arli.grpc.pb.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace arli {

// Orchestrator core: the single AgentHub.Session, the FIFO task queue (SQLite-backed),
// UI waiters (Send / AttachTurn streams), cancel/timeout policy, and the relay point where
// every event is recorded (`steps` + audit) before it reaches the UI. Does not spawn or kill
// processes itself — the watchdog polls take_kill_request() / tick().
class OrchestratorHub {
 public:
  using Clock = std::chrono::steady_clock;
  using EventFn = std::function<bool(const v1::TurnEvent&)>;

  struct KillRequest {
    std::string reason;
  };

  static OrchestratorHub& instance();
  void init(std::shared_ptr<Database> db);

  // ----- Session (AgentHub handler) -----
  std::uint64_t try_attach_session();  // 0 = occupied; otherwise connection generation
  void on_session_end(std::uint64_t generation); // idempotent; queued tasks survive
  bool session_active(std::uint64_t generation) const;
  std::uint64_t session_generation() const; // snapshot for watchdog teardown
  bool on_worker_message(std::uint64_t generation, const v1::WorkerToOrch& msg);
  std::optional<v1::OrchToWorker> pop_outbound(std::uint64_t generation, int wait_ms);
  void wake_outbound();

  // ----- worker state -----
  bool worker_ready() const;
  std::string worker_id() const;
  std::string worker_state() const;  // none | starting | idle | busy | stopping
  std::int64_t busy_task() const;
  void set_worker_starting(bool starting);
  int queue_depth() const;

  // ----- UI side -----
  // Register a sink for a task; returns a waiter id. If the task is already terminal the
  // waiter is created finished. `replay` (optional) receives the steps recorded so far,
  // atomically with the registration — AttachTurn writes them before any live event.
  std::int64_t register_waiter(std::int64_t task_id, EventFn fn, std::vector<StepRow>* replay = nullptr);
  void unregister_waiter(std::int64_t waiter_id);
  // True once the task is terminal and all callbacks returned; false on timeout.
  bool wait_task_finished(std::int64_t waiter_id, int timeout_ms);
  // Claim the next queued task if the worker is idle. Safe to call often.
  void dispatch_next();
  // Cancel the active task of a dialog. queued → cancelled now; running → CancelTask + grace.
  bool cancel_dialog(std::int64_t dialog_id);
  // Mark cancelled and ask the watchdog to kill the worker immediately.
  bool hard_stop_dialog(std::int64_t dialog_id);
  bool deliver_confirm(std::int64_t dialog_id, const std::string& token, bool accept, std::int64_t* command_id = nullptr);
  bool deliver_ask_answer(std::int64_t dialog_id, const std::string& answer, const std::string& question_id = {}, std::int64_t* command_id = nullptr);
  bool interaction_active(std::int64_t task, const v1::TurnEvent& event) const;

  // ----- watchdog hooks -----
  void tick(Clock::time_point now);  // idle/turn timeouts, cancel grace → kill request
  std::optional<KillRequest> take_kill_request();
  void fail_queued(const std::string& reason);  // worker cannot be spawned
  void request_shutdown();                        // Shutdown → worker, no more dispatch
  bool shutting_down() const;

 private:
  OrchestratorHub() = default;

  struct Waiter {
    std::int64_t task_id = 0;
    EventFn fn;
    bool closing = false;  // final event scheduled; no more forwards
    bool done = false;     // final event delivered (or none needed)
    int in_flight = 0;     // callbacks scheduled but not yet returned
  };

  // All *_locked helpers assume mu_ is held. Callbacks are collected and run after unlock;
  // a waiter is "finished" only when done && in_flight == 0, so Send never returns while
  // a callback still holds its ServerWriter.
  using Deferred = std::vector<std::function<void()>>;
  void schedule_locked(std::int64_t waiter_id, Waiter& w, const v1::TurnEvent& ev, bool final, Deferred& out);
  // Record (steps + audit) and forward to every waiter of the task. `final` closes them.
  void relay_locked(std::int64_t task_id, const v1::TurnEvent& ev, bool final, Deferred& out);
  void record_locked(std::int64_t task_id, const v1::TurnEvent& ev);
  void finish_busy_locked(std::int64_t task_id, const std::string& status, const std::string& error,
                          const v1::TurnEvent* final_event, Deferred& out);
  void announce_queue_locked(Deferred& out);
  void request_kill_locked(const std::string& reason);
  bool dispatch_locked(Deferred& out);
  void queue_claim_locked(v1::OrchToWorker msg);
  bool acknowledge_claim(std::uint64_t generation, const v1::CommandAck& ack);
  void retry_claim_locked(Clock::time_point now, Deferred& out);
  void abandon_claim_locked();
  struct PendingClaim {
    v1::OrchToWorker message;
    Clock::time_point sent;
    int attempts = 1;
  };
  struct PendingInteraction {
    v1::OrchToWorker message;
    std::int64_t task = 0, dialog = 0, turn = 0;
    Clock::time_point sent;
    int attempts = 1;
  };
  std::int64_t queue_interaction_locked(v1::OrchToWorker msg);
  bool acknowledge_interaction(std::uint64_t generation, const v1::CommandAck& ack);
  void retry_interactions_locked(Clock::time_point now, Deferred& out);
  void abandon_interactions_locked(std::int64_t task);
  bool dialog_instructions_supported_ = false;
  bool mutation_revert_supported_ = false, mutation_catalog_supported_ = false, workspace_identity_supported_ = false;
  bool interaction_ack_supported_ = false;
  std::map<std::int64_t, PendingInteraction> pending_interactions_;
  bool claim_ack_supported_ = false;
  std::optional<PendingClaim> pending_claim_;
  static void run(Deferred& d);
  static v1::TurnEvent error_event(std::int64_t dialog_id, std::int64_t turn_id, const std::string& msg);

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::shared_ptr<Database> db_;

  std::uint64_t session_generation_ = 0;
  bool session_live_ = false;
  bool worker_starting_ = false;
  bool worker_doomed_ = false;  // kill requested; no dispatch until the session ends
  bool shutting_down_ = false;
  std::string worker_id_;
  std::deque<v1::OrchToWorker> outbound_;

  std::string pending_confirm_;
  bool pending_ask_ = false;
  std::string pending_question_id_;
  std::int64_t busy_task_ = 0;
  std::int64_t busy_dialog_ = 0;
  Clock::time_point busy_since_{};
  Clock::time_point last_event_{};
  int turn_timeout_sec_ = 2700;
  int idle_timeout_sec_ = 600;
  int cancel_grace_sec_ = 10;
  std::set<std::int64_t> cancel_sent_;
  std::optional<Clock::time_point> cancel_deadline_;
  std::optional<KillRequest> kill_request_;
  std::unordered_map<std::int64_t, Clock::time_point> action_started_;  // step_id → Action time

  std::int64_t next_waiter_id_ = 1;
  std::map<std::int64_t, Waiter> waiters_;
  std::map<std::int64_t, int> queue_positions_;
};

}  // namespace arli
