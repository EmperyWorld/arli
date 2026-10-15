// Opt-in reproduction of dialog18 against the actual daemon/worker/model.
#include "lifecycle_harness.hpp"
#include "tools/mutation_journal.hpp"
#include "turn_policy.hpp"
#include <google/protobuf/util/json_util.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sqlite3.h>

using namespace lt;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
std::string read(const fs::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}
void require(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}
json proto(const google::protobuf::Message& message) {
  std::string text;
  require(google::protobuf::util::MessageToJsonString(message, &text).ok(), "protobuf report serialization");
  return json::parse(text);
}
void save(const fs::path& path, const json& report) {
  std::ofstream output(path);
  require(bool(output << report.dump(2) << '\n'), "write report");
}
void seed(const fs::path& fixture, const fs::path& workspace) {
  const auto data = json::parse(read(fixture));
  require(data.at("format") == 1, "fixture version");
  for (const auto& [name, row] : data.at("files").items()) {
    const fs::path relative(name);
    require(!relative.is_absolute() && relative.lexically_normal() == relative &&
            *relative.begin() != "..", "fixture path must stay inside workspace");
    const auto text = row.at("content").get<std::string>();
    require(arli::tools_internal::mutation_hash(text) == row.at("sha256").get<std::string>(), "fixture content checksum");
    fs::create_directories((workspace / relative).parent_path());
    std::ofstream(workspace / relative, std::ios::binary) << text;
  }
}
void snapshot(const fs::path& from, const fs::path& to) {
  fs::create_directories(to);
  for (const auto& entry : fs::recursive_directory_iterator(from)) {
    const auto path = to / entry.path().lexically_relative(from);
    require(!entry.is_symlink(), "output snapshot cannot follow symlinks");
    if (entry.is_directory()) fs::create_directories(path);
    else if (entry.is_regular_file()) fs::copy_file(entry.path(), path);
  }
}
json probe(const fs::path& fixture, const fs::path& workspace, const fs::path& report) {
  Proc process;
  const auto script = fixture.parent_path().parent_path() / "webos_runtime_check.py";
  require(process.start("/usr/bin/python3", {script.string(), workspace.string(), report.string()}, {}), "start independent probe");
  const int code = process.wait(50000);
  if (code < 0) process.kill9();
  require(code == 0, "independent probe infrastructure failed; inspect runtime report");
  return json::parse(read(report));
}
json workflow(const fs::path& root, std::int64_t dialog) {
  sqlite3* raw = nullptr;
  const int opened = sqlite3_open_v2((root / "arli.db").c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
  require(opened == SQLITE_OK, "read-only context database");
  sqlite3_stmt* query = nullptr;
  require(sqlite3_prepare_v2(raw, "SELECT text FROM messages WHERE dialog_id=?1 AND role='context_state' ORDER BY id DESC LIMIT 1",
                            -1, &query, nullptr) == SQLITE_OK, "read-only context query");
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(query, sqlite3_finalize);
  sqlite3_bind_int64(query, 1, dialog);
  if (sqlite3_step(query) != SQLITE_ROW) return json::object();
  const auto* value = sqlite3_column_text(query, 0);
  return json::parse(value ? reinterpret_cast<const char*>(value) : "{}").value("workflow", json::object());
}
bool verified_stage(const json& turn) {
  const auto stage = turn.value("workflow", json::object()).value("stage", json::object());
  if (!stage.value("completed", false) || !stage.contains("checks") || stage["checks"].empty()) return false;
  std::set<std::string> required, passed;
  for (const auto& check : stage["checks"]) required.insert(check.get<std::string>());
  for (const auto& event : turn["events"]) {
    if (!event.contains("observation")) continue;
    const auto& row = event["observation"];
    const auto tool = row.value("tool", "");
    const bool ok = row.value("ok", false);
    if ((ok && arli::tool_writes_workdir(tool)) || tool == "start_background" || tool == "defer_task_stage") passed.clear();
    if (tool != "run_shell") continue;
    const auto args = json::parse(row.value("argsJson", "{}"), nullptr, false);
    const auto command = args.is_object() && args.contains("command") && args["command"].is_string()
        ? args["command"].get<std::string>() : std::string();
    if (!required.contains(command)) passed.clear();
    else if (ok) passed.insert(command); else passed.erase(command);
  }
  return passed == required;
}
json run(Client& client, std::int64_t dialog, int turn, const std::string& prompt) {
  grpc::ClientContext context;
  client.auth(context);
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::minutes(15));
  arli::v1::SendRequest request;
  request.set_dialog_id(dialog);
  request.set_request_id("webos-recovery-" + std::to_string(turn));
  request.set_text(prompt);
  json result = {{"prompt", prompt}, {"turn", turn}, {"events", json::array()},
                 {"confirmations", json::array()}, {"done_received", false}, {"asked_user", false}};
  auto stream = client.stub->Send(&context, request);
  arli::v1::TurnEvent event;
  while (stream->Read(&event)) {
    if (event.has_confirm_needed() && event.interaction_active()) {
      // Explicit test policy: sandboxed commands/mutations in this generated fixture only.
      // Settings below prohibit unsandboxed execution and network access.
      grpc::ClientContext control;
      client.auth(control);
      control.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
      arli::v1::ConfirmDangerRequest answer;
      answer.set_dialog_id(dialog);
      answer.set_confirm_token(event.confirm_needed().confirm_token());
      answer.set_accept(true);
      arli::v1::ConfirmDangerReply reply;
      require(client.stub->ConfirmDanger(&control, answer, &reply).ok() && reply.ok(), "sandbox confirmation delivery");
      result["confirmations"].push_back({{"command", event.confirm_needed().command()}, {"approved", true}});
    }
    // Never persist confirmation credentials or auth material in report artifacts.
    if (event.has_action() || event.has_observation() || event.has_error() || event.has_done() || event.has_plan())
      result["events"].push_back(proto(event));
    if (event.has_observation()) {
      std::cerr << "WEBOS_TURN " << turn << " tool=" << event.observation().tool()
                << " ok=" << event.observation().ok() << '\n';
    }
    if (event.has_ask_user() && event.interaction_active()) {
      result["asked_user"] = true;
      result["question"] = event.ask_user().question();
      client.cancel(dialog);
    }
    if (event.has_done()) {
      result["done"] = proto(event.done());
      result["done_received"] = true;
    }
  }
  const auto status = stream->Finish();
  result["grpc_ok"] = status.ok();
  result["grpc_code"] = status.error_code();
  if (!status.ok()) client.cancel(dialog);
  result["task"] = proto(client.last_task(dialog));
  grpc::ClientContext control;
  client.auth(control);
  control.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
  arli::v1::GetTurnTraceRequest query;
  query.set_dialog_id(dialog); query.set_turn_id(turn);
  arli::v1::GetTurnTraceReply trace;
  if (client.stub->GetTurnTrace(&control, query, &trace).ok()) {
    for (auto& item : *trace.mutable_items()) if (item.kind() == "confirm") item.clear_args_json();
    result["trace"] = proto(trace);
  }
  return result;
}
}

int main(int argc, char** argv) {
  if (argc != 7) {
    std::cerr << "usage: live-webos-eval daemon agent base_url model fixture.json report.json\n";
    return 2;
  }
  char tmp[] = "/tmp/arli-live-webos-recovery-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp), workspace = root / "workspace", fixture = fs::absolute(argv[5]);
  json report = {{"workspace", workspace.string()}, {"state", root.string()}, {"model", argv[4]},
                 {"through_daemon", true}, {"through_qt", false}, {"passed", false},
                 {"claim_review", "Final statements and declared substage require review against traces and runtime results"},
                 {"turns", json::array()}};
  Proc daemon;
  try {
    seed(fixture, workspace);
    report["fixture_sha256"] = arli::tools_internal::mutation_hash(read(fixture));
    report["baseline"] = probe(fixture, workspace, root / "baseline-runtime.json");
    const auto endpoint = "127.0.0.1:" + std::to_string(free_port());
    require(daemon.start(argv[1], {endpoint}, {{"ARLI_STATE_DIR", tmp}, {"ARLI_LISTEN", endpoint},
                                             {"ARLI_AGENT_BIN", argv[2]}}), "start isolated daemon");
    Client client(endpoint);
    require(client.wait_health(8000), "daemon health");
    client.token = arli::secrets::load(root / "ui_token");
    require(client.wait_worker("idle", 8000), "worker ready");
    grpc::ClientContext context;
    client.auth(context);
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(15));
    arli::v1::Settings settings, configured;
    settings.set_llm_base_url(argv[3]); settings.set_llm_model(argv[4]); settings.set_workdir(workspace.string());
    settings.set_context_tokens(49152); settings.set_max_steps(64); settings.set_llm_profile("gemma4");
    settings.set_llm_temperature(0.0); report["temperature"] = 0.0;
    settings.set_turn_timeout_sec(900); settings.set_idle_timeout_sec(180); settings.set_cancel_grace_sec(5);
    settings.set_shell_net(false); settings.set_allow_unsandboxed_shell(false); settings.set_delete_confirm(true);
    require(client.stub->SetSettings(&context, settings, &configured).ok(), "model settings");
    const auto dialog = client.create_dialog("WebOS dialog18 recovery acceptance");
    require(dialog > 0, "create isolated dialog"); report["dialog_id"] = dialog;
    const std::vector<std::string> prompts = {
      "Посмотри код приложения в директории рабочей лежит. и давай его доработаем полноценно.", "Продолжаем"};
    for (int i = 0; i < 2; ++i) {
      auto result = run(client, dialog, i + 1, prompts[i]);
      result["workflow"] = workflow(root, dialog);
      result["declared_stage_commands_passed"] = verified_stage(result);
      const auto copy = root / ("after-turn-" + std::to_string(i + 1));
      snapshot(workspace, copy);
      result["runtime"] = probe(fixture, copy, root / ("runtime-" + std::to_string(i + 1) + ".json"));
      result["snapshot"] = copy.string();
      result["explorer_sha256"] = arli::tools_internal::mutation_hash(read(copy / "src/apps/explorer.js"));
      report["turns"].push_back(result);
      save(argv[6], report);
      require(result["grpc_ok"].get<bool>() && result["done_received"].get<bool>() && !result["asked_user"].get<bool>(), "turn did not complete autonomously");
    }
    const auto& final = report["turns"].back()["runtime"];
    bool no_regressions = true, completed_stages = true, wrote_files = false;
    for (const auto& turn : report["turns"])
      for (const auto& [name, value] : report["baseline"]["checks"].items())
        if (value["passed"].get<bool>() && !turn["runtime"]["checks"][name]["passed"].get<bool>()) no_regressions = false;
    for (const auto& turn : report["turns"]) {
      const auto done = turn.value("done", json::object());
      const auto state = done.value("verificationState", "");
      completed_stages = completed_stages && done.value("outcome", "") == "done" &&
          (state == "partial" || state == "checks_passed") && turn.value("declared_stage_commands_passed", false);
      wrote_files = wrote_files || done.value("editStats", json::object()).value("filesTouched", 0) > 0;
    }
    report["no_regressions"] = no_regressions;
    report["all_functional_checks_passed"] = final["passed"];
    report["completed_verified_stages"] = completed_stages;
    report["actual_file_edits"] = wrote_files;
    // The original request is broad: two bounded stages need not finish every feature.
    // Never turn pre-existing missing rm/readdir/Todo behavior into a regression.
    report["passed"] = no_regressions && completed_stages && wrote_files;
  } catch (const std::exception& error) { report["error"] = error.what(); }
  daemon.term(); if (daemon.wait(8000) < 0) daemon.kill9();
  save(argv[6], report);
  std::cout << "WEBOS_RESULT passed=" << report["passed"] << " report=" << argv[6] << '\n';
  return report["passed"].get<bool>() ? 0 : 1;
}
