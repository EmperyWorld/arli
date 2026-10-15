#include "redact.hpp"
// arli-agent — worker process: one AgentHub.Session, one turn at a time.
#include "agent_worker.hpp"
#include "claim_receipt.hpp"
#include "interaction_receipt.hpp"
#include "auth.hpp"
#include "ask.hpp"
#include "confirm.hpp"
#include "db.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "secrets.hpp"
#include "tools/rlimit_util.hpp"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <signal.h>
#include <unistd.h>

namespace {

using arli::v1::AgentHub;
using arli::v1::OrchToWorker;
using arli::v1::WorkerToOrch;

void on_term(int) {
  // Async-signal-safe: atomics only. The turn loop / heartbeat thread act on them.
  arli::worker::flags().cancel.store(true);
  arli::worker::flags().shutdown.store(true);
}

// Returns false when the worker must exit (Shutdown while idle / SIGTERM).
bool handle(const OrchToWorker& msg, std::thread& claim_thread, const std::shared_ptr<arli::Database>& db) {
  auto& f = arli::worker::flags();
  if ((msg.has_ask_user_answer() || msg.has_confirm_result()) && msg.command_id() > 0) {
    static arli::worker::InteractionReceipt receipts;
    const auto payload = msg.SerializeAsString();
    if (!arli::worker::apply_and_ack(msg.command_id(), [&] {
      if (const auto previous = receipts.lookup(msg.command_id(), payload); previous.has_value()) return *previous;
      const bool accepted = msg.has_ask_user_answer()
          ? arli::AskHub::instance().answer(msg.ask_user_answer().dialog_id(), msg.ask_user_answer().answer(),
                                           msg.ask_user_answer().question_id())
          : arli::ConfirmHub::instance().decide(msg.confirm_result().dialog_id(), msg.confirm_result().confirm_token(),
                                               msg.confirm_result().accept());
      receipts.record(msg.command_id(), payload, accepted);
      return accepted;
    })) arli::worker::cancel_session();
  } else if (msg.has_claim() && msg.command_id() > 0) {
    static arli::worker::ClaimReceipt receipts;
    const auto payload = msg.claim().SerializeAsString();
    const auto previous = receipts.lookup(msg.command_id(), payload);
    const bool fresh = !previous.has_value();
    const bool accepted = fresh ? (!f.shutdown.load() && !f.busy.exchange(true)) : *previous;
    if (fresh) receipts.record(msg.command_id(), payload, accepted);
    WorkerToOrch reply;
    reply.mutable_command_ack()->set_command_id(msg.command_id());
    reply.mutable_command_ack()->set_accepted(accepted);
    if (!arli::worker::write_msg(reply)) {
      if (fresh && accepted) f.busy.store(false);
      arli::worker::cancel_session();
      return true;
    }
    if (fresh && accepted) {
      if (claim_thread.joinable()) claim_thread.join();
      claim_thread = std::thread(arli::worker::run_claim, msg.claim(), db);
    }
  } else if (msg.has_claim()) {
    if (f.shutdown.load()) {
      arli::worker::reject_claim(msg.claim(), "worker shutting down");
    } else if (f.busy.exchange(true)) {
      arli::worker::reject_claim(msg.claim(), "worker busy");
    } else {
      if (claim_thread.joinable()) {
        claim_thread.join();
      }
      claim_thread = std::thread(arli::worker::run_claim, msg.claim(), db);
    }
  } else if (msg.has_cancel()) {
    if (msg.cancel().dialog_id() == f.dialog.load() || msg.cancel().task_id() == f.task.load()) {
      f.cancel.store(true);
      arli::ConfirmHub::instance().reject_dialog(msg.cancel().dialog_id());
      arli::AskHub::instance().reject_dialog(msg.cancel().dialog_id());
    }
  } else if (msg.has_confirm_result()) {
    arli::ConfirmHub::instance().decide(msg.confirm_result().dialog_id(), msg.confirm_result().confirm_token(),
                                        msg.confirm_result().accept());
  } else if (msg.has_ask_user_answer()) {
    arli::AskHub::instance().answer(msg.ask_user_answer().dialog_id(), msg.ask_user_answer().answer(), msg.ask_user_answer().question_id());
  } else if (msg.has_shutdown()) {
    arli::log::info("agent", "Shutdown from orchestrator");
    f.shutdown.store(true);
    return f.busy.load();  // busy: let the turn finish, run_claim leaves the Session after Done
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("arli-agent %s\n", arli::kVersion);
    return 0;
  }
  if (const char* apply_limits = std::getenv("ARLI_WORKER_APPLY_RLIMITS");
      apply_limits && std::strcmp(apply_limits, "1") == 0) {
    // The daemon uses posix_spawn so no application code runs in a forked copy
    // of its multi-threaded process. Apply the worker limits in the fresh image,
    // before opening the database or connecting to the orchestrator.
    arli::apply_agent_rlimits();
    unsetenv("ARLI_WORKER_APPLY_RLIMITS");
  }
  std::string endpoint = arli::kListenEndpoint;
  if (const char* env = std::getenv("ARLI_LISTEN"); env && *env) {
    endpoint = env;
  }
  if (argc >= 2 && argv[1] && *argv[1]) {
    endpoint = argv[1];
  }
  if (!arli::listen_endpoint_allowed(endpoint)) {
    std::fprintf(stderr, "arli-agent: listen refused (only 127.0.0.1 / localhost)\n");
    return 2;
  }

  signal(SIGTERM, on_term);
  signal(SIGINT, on_term);
  signal(SIGPIPE, SIG_IGN);

  arli::redact::enable_persistence(arli::state_root()/"redaction.fingerprints");
  const std::string worker_id = std::string("worker-") + std::to_string(getpid());
  arli::log::info("agent", "connect " + arli::log::kv("endpoint", endpoint) + " " + arli::log::kv("worker", worker_id) +
                               " " + arli::log::kv("db", arli::db_path().string()));

  const std::string internal_token = arli::secrets::internal_token();
  if (internal_token.empty()) {
    std::fprintf(stderr, "arli-agent: no ARLI_INTERNAL_TOKEN (and no %s)\n",
                 arli::secrets::internal_token_path().c_str());
    return 3;
  }

  auto db = std::make_shared<arli::Database>(arli::db_path().string());
  db->init();

  auto& f = arli::worker::flags();
  auto channel = arli::auth::make_channel(endpoint);
  std::unique_ptr<AgentHub::Stub> stub = AgentHub::NewStub(channel);
  std::thread heartbeat(arli::worker::heartbeat_loop);

  for (int attempt = 0; attempt < 80 && !f.shutdown.load(); ++attempt) {
    grpc::ClientContext ctx;
    ctx.set_wait_for_ready(true);
    arli::auth::bearer(ctx, internal_token);
    const char* launch_token = std::getenv("ARLI_WORKER_LAUNCH_TOKEN");
    arli::auth::worker_launch(ctx, launch_token ? launch_token : "");
    arli::worker::set_session_ctx(&ctx);
    std::shared_ptr<arli::worker::Stream> stream = stub->Session(&ctx);
    if (!stream) {
      arli::worker::set_session_ctx(nullptr);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    arli::worker::set_stream(stream);

    WorkerToOrch hello;
    hello.mutable_hello()->set_claim_ack_supported(true);
    hello.mutable_hello()->set_interaction_ack_supported(true);
    hello.mutable_hello()->set_dialog_instructions_supported(true);
    hello.mutable_hello()->set_mutation_revert_supported(true);
    hello.mutable_hello()->set_mutation_catalog_supported(true);
    hello.mutable_hello()->set_workspace_identity_supported(true);
    hello.mutable_hello()->set_worker_id(worker_id);
    hello.mutable_hello()->set_pid(static_cast<int32_t>(getpid()));
    if (!arli::worker::write_msg(hello)) {
      arli::worker::set_stream(nullptr);
      arli::worker::set_session_ctx(nullptr);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    arli::log::info("agent", "Session up, waiting for claims");

    std::thread claim_thread;
    OrchToWorker msg;
    while (stream->Read(&msg)) {
      if (!handle(msg, claim_thread, db)) {
        break;
      }
    }

    f.cancel.store(true);
    if (claim_thread.joinable()) {
      claim_thread.join();
    }
    // Finish() before TryCancel: cancelling here would hide the server status (ALREADY_EXISTS).
    const grpc::Status ended = stream->Finish();
    arli::worker::set_stream(nullptr);
    arli::worker::set_session_ctx(nullptr);
    if (f.shutdown.load()) {
      break;
    }
    // The daemon keeps a single Session. A leftover worker from a previous stack
    // must leave instead of hammering the live one.
    if (ended.error_code() == grpc::StatusCode::ALREADY_EXISTS) {
      arli::log::info("agent", "another worker holds the session, exiting");
      break;
    }
    arli::log::warn("agent", "Session ended, reconnecting: " + ended.error_message());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  f.shutdown.store(true);
  f.exit.store(true);
  heartbeat.join();
  arli::log::info("agent", "exit");
  return 0;
}
