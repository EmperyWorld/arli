// Real local processes and authenticated gRPC; deterministic mock LLM.
#include "lifecycle_harness.hpp"
#include "memory.hpp"
#include "jobs.hpp"
#include "hub.hpp"
#include "tools.hpp"
#include "trace.hpp"
#include "task_settings.hpp"
#include "tools/public_address.hpp"
#include <fstream>
#include <future>
using namespace lt;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
  if (argc != 4) return 2;
  for (const auto* address : {"127.0.0.1", "10.1.2.3", "100.64.1.1", "::1", "::", "fc00::1", "fe80::1", "::ffff:127.0.0.1"})
    LT_CHECK(!arli::public_address(address), "private IPv4/IPv6 addresses blocked");
  LT_CHECK(arli::public_address("8.8.8.8") && arli::public_address("2606:4700:4700::1111"), "public address families allowed");
  char tmp[] = "/tmp/arli-audit-regression-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp), wd = root / "workspace";
  fs::create_directories(wd);
  setenv("ARLI_STATE_DIR", tmp, 1);
  {
    arli::Database db((root / "unit.sqlite").string()); db.init();
    auto d = db.create_dialog("history");
    auto settings = db.get_settings(); settings.workdir = wd.string(); settings.llm_api_key = "TEST_KEY";
    db.set_settings(settings);
    const auto queued = db.enqueue_task(d.id, 1, "queued");
    settings.workdir = "/another/workspace"; settings.read_only = true; db.set_settings(settings);
    const auto snapshot = nlohmann::json::parse(db.get_task(queued.id)->config_json).get<arli::SettingsRow>();
    LT_CHECK(snapshot.workdir == wd.string() && !snapshot.read_only && queued.config_json.find("TEST_KEY") == std::string::npos,
             "queued task pins workspace and policy without copying API key");
    for (int i = 1; i <= 405; ++i) db.add_message(d.id, "user", std::to_string(i), i);
    const auto rows = db.list_messages(d.id, 400);
    LT_CHECK(rows.size() == 400 && rows.front().text == "6" && rows.back().text == "405", "latest history window in chronological order");
    std::vector<arli::MemoryRow> facts;
    for (int i = 0; i < 30; ++i) { arli::MemoryRow m; m.text = "fact" + std::to_string(i); facts.push_back(m); }
    db.replace_memory(facts);
    const auto before = db.list_memory(arli::kMemoryStoreLimit);
    auto memory = arli::MemoryState::load(before);
    memory.remember({{"text", "new fact"}}); db.replace_memory(memory.entries());
    auto after = db.list_memory(arli::kMemoryStoreLimit);
    LT_CHECK(after.size() == 31 && after[1].id == before[0].id, "memory snapshot preserves all facts and existing IDs");
    auto invalid = after; invalid.push_back(after.front());
    bool rejected = false; try { db.replace_memory(invalid); } catch (...) { rejected = true; }
    LT_CHECK(rejected && db.list_memory(200).size() == 31, "invalid memory snapshot rolls back atomically");
    const auto last_id = after.front().id;
    db.replace_memory({});
    auto fresh = arli::MemoryState::load({}, db.next_memory_id());
    fresh.remember({{"text", "after clearing"}});
    LT_CHECK(fresh.entries().front().id > last_id, "memory IDs are never reused after clearing");
    setenv("ARLI_INTERNAL_TOKEN", "SYNTHETIC_TEST_SECRET", 1);
    const auto environment = arli::tool_run_shell(wd, "env");
    LT_CHECK(environment.ok && environment.output.find("SYNTHETIC_TEST_SECRET") == std::string::npos,
             "working shell has no inherited token");
    arli::ShellRunOptions readonly; readonly.read_only = true;
    const auto denied = arli::tool_run_shell(wd, "python3 -c 'open(\"forbidden.txt\",\"w\").write(\"x\")'", readonly);
    LT_CHECK(!denied.ok && !fs::exists(wd / "forbidden.txt"), "read-only filesystem blocks interpreter writes");
    const auto bg = arli::execute_shell(db, wd,
        "python3 -c 'open(\"background.txt\",\"w\").write(\"ok\")'", {}, true, {});
    for (int i = 0; i < 100 && !fs::exists(wd / "background.txt"); ++i) sleep_ms(20);
    LT_CHECK(bg.ok && fs::exists(wd / "background.txt"), "sandboxed background job actually executes");
    const auto escape = arli::execute_shell(db, wd,
        "python3 -c 'open(\"" + (root / "escape.txt").string() + "\",\"w\").write(\"x\")'", {}, true, {});
    sleep_ms(400);
    LT_CHECK(escape.ok && !fs::exists(root / "escape.txt"), "background cannot write outside workspace");
    std::atomic<bool> cancel{true}; arli::ShellRunOptions cancelled; cancelled.cancel = &cancel;
    const auto stopped = arli::tool_run_shell(wd, "sleep 30", cancelled);
    LT_CHECK(!stopped.ok && stopped.output.find("cancelled") != std::string::npos, "shell responds to cancellation");
    unsetenv("ARLI_INTERNAL_TOKEN");
  }
  {
    auto db = std::make_shared<arli::Database>((root / "hub.sqlite").string()); db->init();
    auto settings = db->get_settings(); settings.idle_timeout_sec = 1; db->set_settings(settings);
    const auto dialog = db->create_dialog("frame correlation");
    const auto task = db->enqueue_task(dialog.id, 1, "hi");
    auto& hub = arli::OrchestratorHub::instance(); hub.init(db);
    const auto generation = hub.try_attach_session();
    LT_CHECK(generation, "test session attached");
    arli::v1::WorkerToOrch frame; frame.mutable_hello()->set_worker_id("test-worker"); frame.mutable_hello()->set_pid(1); frame.mutable_hello()->set_workspace_identity_supported(true);
    hub.on_worker_message(generation, frame); // dispatches queued task
    frame.Clear(); frame.set_task_id(task.id); frame.set_dialog_id(dialog.id); frame.set_turn_id(99);
    frame.mutable_done()->set_outcome("done"); hub.on_worker_message(generation, frame);
    LT_CHECK(db->get_task(task.id)->status == "running", "mismatched turn cannot complete task");
    frame.clear_done(); frame.set_turn_id(1); frame.mutable_confirm_needed()->set_confirm_token("pending-token");
    hub.on_worker_message(generation, frame);
    LT_CHECK(!hub.deliver_confirm(dialog.id, "wrong", true), "wrong pending token rejected");
    LT_CHECK(hub.deliver_confirm(dialog.id, "pending-token", true) && !hub.deliver_confirm(dialog.id, "pending-token", true),
             "confirmation consumed exactly once");
    sleep_ms(2100);
    frame.clear_confirm_needed(); frame.mutable_heartbeat(); hub.on_worker_message(generation, frame);
    hub.tick(arli::OrchestratorHub::Clock::now());
    LT_CHECK(db->get_task(task.id)->error == "idle_timeout", "heartbeat does not hide absent task progress");
    hub.on_session_end(generation);
  }
  Proc daemon, mock, second;
  const int port = free_port(); const auto url = "http://127.0.0.1:" + std::to_string(port) + "/v1";
  mock.start(argv[3], {std::to_string(port)}, {{"ARLI_MOCK_MODELS_DELAY_MS", "1200"}});
  const auto ep = "127.0.0.1:" + std::to_string(free_port());
  const std::vector<std::pair<std::string,std::string>> vars = {
      {"ARLI_STATE_DIR", tmp}, {"ARLI_AGENT_BIN", argv[2]}, {"ARLI_LISTEN", ep}};
  daemon.start(argv[1], {ep}, vars); Client c(ep);
  if (!c.wait_health(8000)) { daemon.kill9(); mock.kill9(); return 3; }
  c.token = arli::secrets::load(root / "ui_token");
  LT_CHECK(c.wait_worker("idle", 8000), "worker connected");
  {
    arli::Database jobs_db((root / "arli.db").string()); jobs_db.init();
    const auto started = arli::execute_shell(jobs_db, wd, "printf grpc-job; exit 7", "", true);
    LT_CHECK(started.ok, "background job launched for RPC inspection");
    const auto job_id = jobs_db.list_jobs().front().id;
    bool finished = false;
    for (int attempt = 0; attempt < 100 && !finished; ++attempt) {
      grpc::ClientContext ctx; c.auth(ctx);
      ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
      arli::v1::ListJobsRequest req; req.set_include_finished(true);
      arli::v1::ListJobsReply reply;
      if (c.stub->ListJobs(&ctx, req, &reply).ok()) for (const auto& job : reply.jobs())
        if (job.id() == job_id && job.status() == "exited")
          finished = job.exit_code_known() && job.exit_code() == 7 && job.log_tail() == "grpc-job";
      if (!finished) sleep_ms(20);
    }
    LT_CHECK(finished, "authenticated RPC exposes actual exit code and final output");
  }
  for (const std::string token : {"", "forged-launch"}) {
    auto hub_stub = arli::v1::AgentHub::NewStub(arli::auth::make_channel(ep));
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
    arli::auth::bearer(ctx, arli::secrets::load(root / "internal_token"));
    arli::auth::worker_launch(ctx, token);
    auto stream = hub_stub->Session(&ctx);
    stream->WritesDone();
    arli::v1::OrchToWorker reply; while (stream->Read(&reply)) {}
    LT_CHECK(stream->Finish().error_code() == grpc::StatusCode::UNAUTHENTICATED,
             "persistent internal token cannot acquire worker Session without launch identity");
  }
  LT_CHECK(c.wait_worker("idle", 1000), "rejected sessions do not disturb worker");
  std::ofstream(wd / "config.json") << "{\"value\":1}\n";
  c.set_model(url, "syntax_stuck", wd.string(), 600, 5);
  LT_CHECK(c.health(), "Health meets 700ms deadline despite 1200ms model endpoint");
  const auto d = c.create_dialog("failed verification");
  const auto sent = c.send(d, "Исправь config.json: value должна быть 2");
  LT_CHECK(sent.error_contains("Проверка не прошла") && c.last_task(d).status() == "failed", "failed verification is a failed task");
  for (const auto& ev : sent.events) if (ev.has_done()) {
    auto step = arli::trace::step_from_event(ev, 1);
    LT_CHECK(ev.done().outcome() == "failed" && step && arli::trace::event_from_step(*step).done().outcome() == "failed",
             "outcome survives trace replay");
  }
  { grpc::ClientContext ctx; c.auth(ctx); arli::v1::ConfirmDangerRequest req;
    req.set_dialog_id(d); req.set_confirm_token("invalid"); req.set_accept(true);
    arli::v1::ConfirmDangerReply rep;
    LT_CHECK(c.stub->ConfirmDanger(&ctx, req, &rep).ok() && !rep.ok(), "invalid confirmation rejected"); }
  { grpc::ClientContext ctx; c.auth(ctx); arli::v1::AnswerAskUserRequest req;
    req.set_dialog_id(d); req.set_answer("stale"); arli::v1::AnswerAskUserReply rep;
    LT_CHECK(c.stub->AnswerAskUser(&ctx, req, &rep).ok() && !rep.ok(), "answer without pending question rejected"); }
  c.set_model(url, "slow:2500", wd.string(), 600, 5);
  const auto active_id = c.create_dialog("duplicate daemon");
  auto active = std::async(std::launch::async, [&] { return c.send(active_id, "hi", 10000); });
  for (int i = 0; i < 100 && c.last_task(active_id).status() != "running"; ++i) sleep_ms(20);
  LT_CHECK(c.last_task(active_id).status() == "running", "task running before duplicate start");
  second.start(argv[1], {ep}, vars);
  LT_CHECK(second.wait(1500) != 0, "second daemon cannot own database");
  LT_CHECK(c.last_task(active_id).status() == "running", "duplicate startup does not reconcile live task");
  { grpc::ClientContext ctx; c.auth(ctx); arli::v1::DeleteDialogRequest req;
    req.set_dialog_id(active_id); arli::v1::DeleteDialogReply rep;
    LT_CHECK(c.stub->DeleteDialog(&ctx, req, &rep).error_code() == grpc::StatusCode::FAILED_PRECONDITION,
             "cannot delete an active dialog"); }
  LT_CHECK(active.get().done() && c.last_task(active_id).status() == "done", "original daemon completes task");
  daemon.term(); if (daemon.wait(8000) < 0) daemon.kill9();
  second.kill9(); mock.kill9(); fs::remove_all(root);
  return g_failed ? 1 : 0;
}
