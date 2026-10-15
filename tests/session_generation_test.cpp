// Deterministic interleavings that are difficult to reproduce with socket timing.
#include "hub.hpp"
#include <filesystem>
#include <iostream>
#include <unistd.h>
int main() {
  char tmp[] = "/tmp/arli-session-generation-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  int failures = 0;
  auto check = [&](bool ok, const char* text) { if (!ok) { std::cerr << text << '\n'; ++failures; } };
  {
    auto db = std::make_shared<arli::Database>(std::string(tmp) + "/test.sqlite");
    db->init();
    auto& hub = arli::OrchestratorHub::instance(); hub.init(db);
    const auto old = hub.try_attach_session();
    check(old && !hub.try_attach_session(), "single session admission");
    arli::v1::WorkerToOrch frame; frame.mutable_heartbeat();
    check(!hub.on_worker_message(old, frame), "heartbeat before Hello rejected");
    frame.Clear(); frame.mutable_hello();
    check(!hub.on_worker_message(old, frame), "empty Hello rejected");
    frame.mutable_hello()->set_worker_id("bad\nidentity"); frame.mutable_hello()->set_pid(1); frame.mutable_hello()->set_workspace_identity_supported(true);
    check(!hub.on_worker_message(old, frame), "control characters rejected");
    frame.mutable_hello()->set_worker_id("old-worker");
    check(hub.on_worker_message(old, frame), "valid Hello accepted");
    check(!hub.on_worker_message(old, frame), "duplicate Hello rejected");
    hub.on_session_end(old);
    const auto current = hub.try_attach_session();
    check(current && current != old, "new generation issued");
    frame.mutable_hello()->set_worker_id("new-worker");
    check(hub.on_worker_message(current, frame), "new worker ready");
    const auto dialog = db->create_dialog("new generation");
    const auto task = db->enqueue_task(dialog.id, 1, "test");
    hub.dispatch_next();
    check(!hub.pop_outbound(old, 0), "old writer cannot steal new claim");
    const auto claim = hub.pop_outbound(current, 0);
    check(claim && claim->has_claim() && claim->claim().task_id() == task.id, "claim delivered to new writer");
    frame.Clear(); frame.set_task_id(task.id); frame.set_dialog_id(dialog.id); frame.set_turn_id(1);
    frame.mutable_done()->set_outcome("done");
    check(!hub.on_worker_message(old, frame), "old worker cannot complete new task even with matching IDs");
    hub.on_session_end(old);
    check(hub.session_active(current) && db->get_task(task.id)->status == "running",
          "late old teardown cannot fail new task");
    const auto touched = db->get_task(task.id)->updated_at;
    sleep(1); // database timestamps have second precision
    frame.clear_done(); frame.mutable_heartbeat(); frame.set_turn_id(2);
    check(hub.on_worker_message(current, frame) && db->get_task(task.id)->status == "running",
          "late heartbeat discarded without closing healthy stream");
    check(db->get_task(task.id)->updated_at == touched, "foreign heartbeat cannot refresh task timestamp");
    frame.set_turn_id(1);
    check(hub.on_worker_message(current, frame), "correlated heartbeat accepted");
    check(db->get_task(task.id)->updated_at != touched, "matching heartbeat refreshes its task");
    frame.clear_heartbeat(); frame.mutable_done()->set_outcome("done");
    check(hub.on_worker_message(current, frame) && db->get_task(task.id)->status == "done",
          "current worker completes its own task");
    hub.on_session_end(current);
    check(!hub.session_active(current), "current teardown applied");
  }
  std::filesystem::remove_all(tmp);
  return failures ? 1 : 0;
}
