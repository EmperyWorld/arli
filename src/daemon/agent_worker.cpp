#include "agent_worker.hpp"

#include "turn.hpp"
#include "worker_revert.hpp"
#include "workspace_identity.hpp"
#include "redact.hpp"
#include "grpc_registry.hpp"

#include <chrono>
#include <mutex>
#include <thread>

namespace arli::worker {
namespace {

constexpr int kHeartbeatSec = 15;

std::mutex g_write_mu;
std::shared_ptr<Stream> g_stream;
std::mutex g_ctx_mu;
grpc::ClientContext* g_session_ctx = nullptr;

v1::WorkerToOrch frame_for_active() {
  auto& f = flags();
  v1::WorkerToOrch msg;
  msg.set_task_id(f.task.load());
  msg.set_dialog_id(f.dialog.load());
  msg.set_turn_id(f.turn.load());
  return msg;
}

void emit_event(const v1::TurnEvent& ev) {
  v1::WorkerToOrch msg = frame_for_active();
  msg.set_dialog_id(ev.dialog_id());
  msg.set_turn_id(ev.turn_id());
  if (ev.has_thought()) {
    *msg.mutable_thought() = ev.thought();
  } else if (ev.has_token()) {
    *msg.mutable_token() = ev.token();
  } else if (ev.has_action()) {
    *msg.mutable_action() = ev.action();
  } else if (ev.has_observation()) {
    *msg.mutable_observation() = ev.observation();
  } else if (ev.has_ask_user()) {
    *msg.mutable_ask_user() = ev.ask_user();
  } else if (ev.has_status()) {
    *msg.mutable_status() = ev.status();
  } else if (ev.has_error()) {
    *msg.mutable_error() = ev.error();
  } else if (ev.has_done()) {
    // The next Claim may arrive immediately after Done. The reader joins this turn
    // before starting the next one, so release admission before publishing Done.
    flags().busy.store(false);
    *msg.mutable_done() = ev.done();
  } else if (ev.has_confirm_needed()) {
    *msg.mutable_confirm_needed() = ev.confirm_needed();
  } else if (ev.has_file_pass()) {
    *msg.mutable_file_pass() = ev.file_pass();
  } else if (ev.has_context_use()) {
    *msg.mutable_context_use() = ev.context_use();
  } else if (ev.has_plan()) {
    *msg.mutable_plan() = ev.plan();
  } else if (ev.has_memory()) {
    *msg.mutable_memory() = ev.memory();
  } else {
    return;
  }
  write_msg(std::move(msg));
}

}  // namespace

Flags& flags() {
  static Flags f;
  return f;
}

void set_stream(std::shared_ptr<Stream> s) {
  std::lock_guard lock(g_write_mu);
  g_stream = std::move(s);
}

void set_session_ctx(grpc::ClientContext* ctx) {
  std::lock_guard lock(g_ctx_mu);
  g_session_ctx = ctx;
}

bool write_msg(v1::WorkerToOrch msg) {
  std::lock_guard lock(g_write_mu);
  if (!g_stream) {
    return false;
  }
  return g_stream->Write(msg);
}

bool apply_and_ack(std::int64_t id, const std::function<bool()>& apply) {
  std::lock_guard lock(g_write_mu);
  if (!g_stream) return false;
  v1::WorkerToOrch reply;
  reply.mutable_command_ack()->set_command_id(id);
  reply.mutable_command_ack()->set_accepted(apply());
  return g_stream->Write(reply);
}

void cancel_session() {
  std::lock_guard lock(g_ctx_mu);
  if (g_session_ctx) {
    g_session_ctx->TryCancel();
  }
}

void heartbeat_loop() {
  auto& f = flags();
  int ticks = 0;
  while (!f.exit.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if (f.shutdown.load() && !f.busy.load() && !f.task.load()) {
      cancel_session();
    }
    if (++ticks < kHeartbeatSec * 4) {
      continue;
    }
    ticks = 0;
    if (f.busy.load() && f.task.load() != 0) {
      v1::WorkerToOrch hb = frame_for_active();
      hb.mutable_heartbeat();
      write_msg(std::move(hb));
    }
  }
}

void run_claim(v1::ClaimTask claim, std::shared_ptr<Database> db) {
  auto& f = flags();
  f.cancel.store(false);
  f.task.store(claim.task_id());
  f.dialog.store(claim.dialog_id());
  f.turn.store(claim.turn_id());

  // Orchestrator owns settings; the claim carries the per-turn config. Never write it back.
  TurnConfig cfg = TurnConfig::from_settings(db->get_settings());
  if (!claim.llm_base_url().empty()) {
    cfg.llm_base_url = claim.llm_base_url();
  }
  if (!claim.llm_model().empty()) {
    cfg.llm_model = claim.llm_model();
  }
  cfg.llm_api_key = claim.llm_api_key();
  if (!claim.llm_profile().empty()) {
    cfg.llm_profile = claim.llm_profile();
  }
  cfg.llm_temperature = claim.has_llm_temperature() ? claim.llm_temperature() : -1.0;
  if (!claim.workdir().empty()) {
    cfg.workdir = claim.workdir();
  }
  cfg.dialog_instructions = claim.dialog_instructions();
  cfg.shell_auto = claim.shell_auto();
  cfg.shell_ask = claim.shell_ask();
  cfg.shell_allow_all = claim.shell_allow_all();
  cfg.delete_confirm = claim.delete_confirm();
  cfg.list_ignore = claim.list_ignore();
  if (claim.context_tokens() > 0) {
    cfg.context_tokens = claim.context_tokens();
  }
  if (claim.max_steps() > 0) {
    cfg.max_steps = claim.max_steps();
  }
  if (claim.shell_timeout_sec() > 0) {
    cfg.shell_timeout_sec = claim.shell_timeout_sec();
  }
  cfg.shell_net = claim.shell_net();
  cfg.read_only = claim.read_only();
  cfg.allow_unsandboxed_shell = claim.allow_unsandboxed_shell();

  TurnControl ctl;
  ctl.cancel = &f.cancel;
  ctl.task_id = claim.task_id();
  try {
    WorkspaceLease workspace(cfg.workdir,claim.workspace_identity_json());
    ctl.check_workspace=[&workspace]{workspace.check();};
    if (!claim.revert_changeset_id().empty()) {
      run_revert(*db,claim,[](const v1::TurnEvent& ev){emit_event(ev);return !flags().cancel.load();},&f.cancel,ctl.check_workspace);
    } else {
    auto grpc_tools = load_grpc_registry();
    ctl.grpc_tools = grpc_tools.get();
    run_turn(*db, cfg, claim.dialog_id(), claim.user_text(), claim.turn_id(),
             [&f](const v1::TurnEvent& ev) {
               emit_event(ev);
               return !f.cancel.load();
             },
             ctl);
    }
  } catch (const std::exception& e) {
    // Fatal for this task only: the orchestrator marks it failed and moves on.
    v1::WorkerToOrch msg = frame_for_active();
    f.busy.store(false);
    msg.mutable_task_failed()->set_message(redact::text(std::string("worker exception: ") + e.what()));
    write_msg(std::move(msg));
  }
  f.task.store(0);
  f.dialog.store(0);
  f.turn.store(0);
  f.busy.store(false);
  if (f.shutdown.load()) {
    // SIGTERM / Shutdown arrived mid-turn: the turn is over, now leave the Session cleanly.
    cancel_session();
  }
}

void reject_claim(const v1::ClaimTask& claim, const std::string& reason) {
  v1::WorkerToOrch msg;
  msg.set_task_id(claim.task_id());
  msg.set_dialog_id(claim.dialog_id());
  msg.set_turn_id(claim.turn_id());
  msg.mutable_claim_rejected()->set_reason(reason);
  write_msg(std::move(msg));
}

}  // namespace arli::worker
