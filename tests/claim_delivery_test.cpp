#include "hub.hpp"
#include "claim_receipt.hpp"
#include <filesystem>
#include <iostream>
#include <unistd.h>
int main() {
  char tmp[] = "/tmp/arli-claim-delivery-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  int failures = 0;
  auto check = [&](bool ok, const char* text) { if (!ok) { std::cerr << text << '\n'; ++failures; } };
  const std::string file = std::string(tmp) + "/test.sqlite";
  std::int64_t accepted_id = 0, uncertain_id = 0;
  {
    auto db = std::make_shared<arli::Database>(file); db->init();
    auto& hub = arli::OrchestratorHub::instance(); hub.init(db);
    auto generation = hub.try_attach_session();
    arli::v1::WorkerToOrch hello;
    hello.mutable_hello()->set_worker_id("receipt-worker"); hello.mutable_hello()->set_pid(1); hello.mutable_hello()->set_workspace_identity_supported(true);
    hello.mutable_hello()->set_claim_ack_supported(true);
    hello.mutable_hello()->set_dialog_instructions_supported(true);
    check(hub.on_worker_message(generation, hello), "capability handshake");
    const auto dialog = db->create_dialog("delivery");
    check(db->set_dialog_instructions(dialog.id, "ORIGINAL_RULE"), "set original rules");
    auto settings = db->get_settings();
    check(settings.llm_temperature == -1.0, "legacy settings keep profile sampling default");
    settings.llm_temperature = 0.0; db->set_settings(settings);
    const auto task = db->enqueue_task(dialog.id, 1, "test");
    settings.llm_temperature = 1.5; db->set_settings(settings);
    db->set_dialog_instructions(dialog.id, "UPDATED_RULE");
    hub.dispatch_next();
    auto first = hub.pop_outbound(generation, 0);
    if (!first) return 2;
    check(first->claim().dialog_instructions() == "ORIGINAL_RULE", "queued rules snapshot is immutable");
    check(first->claim().has_llm_temperature() && first->claim().llm_temperature() == 0.0,
          "explicit zero sampling survives queue and later settings changes");
    accepted_id = first->command_id();
    check(accepted_id > 0 && db->command_receipt_status(accepted_id) == "pending", "journal before delivery");
    arli::worker::ClaimReceipt cache;
    int executions = 0;
    auto receive = [&](const arli::v1::OrchToWorker& msg) {
      const auto payload = msg.claim().SerializeAsString();
      auto previous = cache.lookup(msg.command_id(), payload);
      if (!previous) { ++executions; cache.record(msg.command_id(), payload, true); }
      return previous.value_or(true);
    };
    check(receive(*first), "first claim accepted");
    // Drop the first ack, then move the watchdog clock forward.
    const auto now = arli::OrchestratorHub::Clock::now();
    hub.tick(now + std::chrono::seconds(4));
    auto retry = hub.pop_outbound(generation, 0);
    check(retry && retry->SerializeAsString() == first->SerializeAsString(), "retry is same ID and payload");
    if (retry) check(receive(*retry) && executions == 1, "lost ack does not repeat execution");
    arli::v1::WorkerToOrch ack;
    ack.mutable_command_ack()->set_command_id(accepted_id); ack.mutable_command_ack()->set_accepted(true);
    check(!hub.on_worker_message(generation + 1, ack), "foreign session ack denied");
    check(hub.on_worker_message(generation, ack), "matching ack accepted");
    check(db->command_receipt_status(accepted_id) == "accepted", "receipt persisted");
    hub.tick(now + std::chrono::seconds(8));
    check(!hub.pop_outbound(generation, 0), "ack stops retries");
    check(hub.on_worker_message(generation, ack), "duplicate ack harmless");
    auto changed = *first; changed.mutable_claim()->set_user_text("changed");
    check(!receive(changed) && executions == 1, "same ID changed payload never executes");
    arli::v1::WorkerToOrch done;
    done.set_task_id(task.id); done.set_dialog_id(dialog.id); done.set_turn_id(1);
    done.mutable_done()->set_outcome("done");
    hub.on_worker_message(generation, done);
    const auto next = db->enqueue_task(dialog.id, 2, "next"); hub.dispatch_next();
    auto second = hub.pop_outbound(generation, 0);
    if (!second) return 2;
    check(second->claim().llm_temperature() == 1.5, "new task captures updated sampling");
    uncertain_id = second->command_id();
    check(uncertain_id > accepted_id && receive(*second), "next delivery gets fresh ID");
    check(!receive(*first) && executions == 2, "evicted older ID never executes");
    for (int seconds : {4, 8, 12}) {
      hub.tick(arli::OrchestratorHub::Clock::now() + std::chrono::seconds(seconds));
      hub.pop_outbound(generation, 0);
    }
    check(db->get_task(next.id)->error == "claim_ack_timeout", "missing ack fails explicitly");
    check(db->command_receipt_status(uncertain_id) == "unconfirmed", "uncertain delivery recorded");
    check(hub.take_kill_request().has_value(), "uncertain worker stopped");
    hub.on_session_end(generation);
    generation = hub.try_attach_session();
    hello.mutable_hello()->set_dialog_instructions_supported(false);
    check(hub.on_worker_message(generation, hello), "legacy worker hello");
    const auto unsupported = db->enqueue_task(dialog.id, 3, "rules required");
    hub.dispatch_next();
    check(!hub.pop_outbound(generation, 0), "rules never silently dropped for old worker");
    check(db->get_task(unsupported.id)->error == "worker_capability_missing", "capability failure explicit");
    hub.on_session_end(generation);
    generation=hub.try_attach_session();hello.mutable_hello()->set_dialog_instructions_supported(true);hello.mutable_hello()->set_workspace_identity_supported(false);
    check(hub.on_worker_message(generation,hello),"worker without workspace capability");const auto unpinned=db->enqueue_task(dialog.id,4,"identity required");hub.dispatch_next();
    check(!hub.pop_outbound(generation,0)&&db->get_task(unpinned.id)->error=="worker_capability_missing","workspace identity never silently discarded");hub.on_session_end(generation);
  }
  {
    arli::Database reopened(file); reopened.init();
    check(reopened.get_settings().llm_temperature == 1.5, "sampling persists across database restart");
    check(reopened.schema_version() == 16, "schema migration");
    check(reopened.command_receipt_status(accepted_id) == "accepted" &&
          reopened.command_receipt_status(uncertain_id) == "unconfirmed", "receipt results survive restart");
    const auto d = reopened.create_dialog("crash");
    const auto t = reopened.enqueue_task(d.id, 1, "crash");
    reopened.mark_running(t.id, "dead");
    const auto id = reopened.create_command_receipt(t.id);
    reopened.fail_running_tasks("worker_lost");
    check(reopened.command_receipt_status(id) == "unconfirmed", "recovery resolves pending receipt without replay");
  }
  std::filesystem::remove_all(tmp);
  return failures ? 1 : 0;
}
