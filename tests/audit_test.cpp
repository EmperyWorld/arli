// arli-test-audit — Э3: steps survive in SQLite, OpenDialog.last_trace, GetTurnTrace,
// AttachTurn replays a live turn, a dropped UI stream does not cancel it, audit has no secrets.
// Usage: arli-test-audit <arli-daemon> <arli-agent> <arli-mock-llm>
#include "lifecycle_harness.hpp"

#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <sstream>

namespace fs = std::filesystem;
using namespace lt;

namespace {

constexpr const char* kSecret = "sk-audit-secret-do-not-leak";

struct Env {
  std::string daemon_bin, agent_bin, mock_bin, state_dir, workdir, endpoint, llm_url;
  Proc mock, daemon;

  bool start(Client& c) {
    if (!daemon.start(daemon_bin, {endpoint},
                      {{"ARLI_STATE_DIR", state_dir}, {"ARLI_AGENT_BIN", agent_bin}, {"ARLI_LISTEN", endpoint}})) {
      return false;
    }
    if (!c.wait_health(5000)) {
      return false;
    }
    c.token = arli::secrets::load(fs::path(state_dir) / "ui_token");
    return !c.token.empty() && c.wait_worker("idle", 6000);
  }
};

bool configure(Client& c, const Env& env, const std::string& model) {
  grpc::ClientContext ctx;
  c.auth(ctx);
  arli::v1::Settings s;
  s.set_llm_base_url(env.llm_url);
  s.set_llm_model(model);
  s.set_workdir(env.workdir);
  s.set_llm_api_key(kSecret);
  s.set_idle_timeout_sec(600);
  s.set_cancel_grace_sec(10);
  arli::v1::Settings out;
  return c.stub->SetSettings(&ctx, s, &out).ok() && out.has_api_key() && out.llm_api_key().empty();
}

std::vector<arli::v1::TraceItem> trace_of(Client& c, std::int64_t dialog, std::int64_t turn) {
  std::vector<arli::v1::TraceItem> result;std::int64_t after=0;
  do{
    grpc::ClientContext ctx;c.auth(ctx);
    arli::v1::GetTurnTraceRequest req;req.set_dialog_id(dialog);req.set_turn_id(turn);
    req.set_after_id(after);req.set_page_size(2);
    arli::v1::GetTurnTraceReply rep;
    if(!c.stub->GetTurnTrace(&ctx,req,&rep).ok())break;
    result.insert(result.end(),rep.items().begin(),rep.items().end());
    if(rep.next_after_id()<=after)break;after=rep.next_after_id();
  }while(true);
  return result;
}

bool has_kind(const std::vector<arli::v1::TraceItem>& items, const std::string& kind) {
  for (const auto& it : items) {
    if (it.kind() == kind) {
      return true;
    }
  }
  return false;
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool tree_contains(const fs::path& root, const std::string& needle) {
  std::error_code ec;
  if (!fs::exists(root, ec)) {
    return false;
  }
  for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    if (read_file(it->path()).find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void t_trace_and_audit(Env& env, Client& c) {
  std::fprintf(stderr, "[trace + audit]\n");
  LT_CHECK(configure(c, env, "tool_read"), "SetSettings keeps api key write-only");
  {
    std::ofstream readme(env.workdir + "/README.md");
    readme << std::string(5000, 'Q');
  }
  const auto d = c.create_dialog("audit");
  const auto sent = c.send(d, "прочитай README");
  LT_CHECK(sent.done() && sent.done_text().find("read ok") != std::string::npos, "tool_read turn done");
  const auto items = trace_of(c, d, 0);
  LT_CHECK(has_kind(items, "action") && has_kind(items, "observation") && has_kind(items, "done"),
           "GetTurnTrace has action, observation, done");
  std::size_t obs = 0;
  for (const auto& it : items) {
    if (it.kind() == "observation") {
      obs = it.text().size();
    }
  }
  LT_CHECK(obs > 4096, "full observation kept in steps");

  grpc::ClientContext ctx;
  c.auth(ctx);
  arli::v1::OpenDialogRequest oreq;
  oreq.set_dialog_id(d);
  arli::v1::OpenDialogReply orep;
  LT_CHECK(c.stub->OpenDialog(&ctx, oreq, &orep).ok(), "OpenDialog");
  LT_CHECK(orep.last_trace_size() == static_cast<int>(items.size()), "OpenDialog.last_trace == GetTurnTrace");
  int tagged = 0;
  for (const auto& m : orep.messages()) {
    if (m.turn_id() > 0) {
      ++tagged;
    }
  }
  LT_CHECK(tagged >= 2, "user and assistant messages carry turn_id");

  const auto task = c.last_task(d);
  const fs::path dir = fs::path(env.state_dir) / "audit" / std::to_string(task.id());
  const std::string events = read_file(dir / "events.jsonl");
  const std::string meta = read_file(dir / "task.json");
  LT_CHECK(events.find("\"kind\":\"observation\"") != std::string::npos, "events.jsonl has the observation");
  LT_CHECK(events.find("bytes cut") != std::string::npos, "audit output truncated to 4 KB");
  LT_CHECK(meta.find("\"status\": \"done\"") != std::string::npos, "task.json closed as done");
  LT_CHECK(!tree_contains(fs::path(env.state_dir) / "audit", kSecret), "audit has no api key");
  LT_CHECK(!tree_contains(fs::path(env.state_dir) / "audit", "Bearer"), "audit has no bearer");
  const std::string log = read_file(fs::path(env.state_dir) / "arli.log");
  LT_CHECK(log.find(kSecret) == std::string::npos && log.find("tool=read_file") != std::string::npos,
           "arli.log has tool lines and no secret");
}

void t_temperature(Client& c) {
  auto get = [&] {
    grpc::ClientContext ctx; c.auth(ctx);
    arli::v1::Settings value;
    LT_CHECK(c.stub->GetSettings(&ctx, arli::v1::GetSettingsRequest(), &value).ok(), "sampling settings readable");
    return value;
  };
  auto put = [&](const arli::v1::Settings& value, arli::v1::Settings& out) {
    grpc::ClientContext ctx; c.auth(ctx);
    return c.stub->SetSettings(&ctx, value, &out);
  };
  auto settings = get(); arli::v1::Settings out;
  LT_CHECK(settings.has_llm_temperature() && settings.llm_temperature() == -1.0, "default sampling exposed explicitly");
  settings.set_llm_temperature(0.0);
  LT_CHECK(put(settings, out).ok() && out.has_llm_temperature() && out.llm_temperature() == 0.0, "RPC accepts explicit zero");
  settings.clear_llm_temperature();
  LT_CHECK(put(settings, out).ok() && out.llm_temperature() == 0.0, "legacy absent field preserves stored zero");
  for (double value : {-0.5, 2.5, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    settings.set_llm_temperature(value);
    LT_CHECK(put(settings, out).error_code() == grpc::StatusCode::INVALID_ARGUMENT, "invalid sampling rejected by RPC");
    LT_CHECK(get().llm_temperature() == 0.0, "rejected sampling does not alter stored settings");
  }
  settings.set_llm_temperature(-1.0);
  LT_CHECK(put(settings, out).ok() && out.llm_temperature() == -1.0, "profile sampling default can be restored");
  settings = get(); settings.set_context_tokens(1);
  LT_CHECK(put(settings,out).error_code()==grpc::StatusCode::INVALID_ARGUMENT,
           "undersized context rejected without persistence");
  settings = get(); settings.set_shell_auto(std::string(70000,'x'));
  LT_CHECK(put(settings,out).error_code()==grpc::StatusCode::INVALID_ARGUMENT,
           "oversized individual setting rejected");
  settings = get(); settings.set_llm_api_key(std::string(530000,'x'));
  LT_CHECK(put(settings,out).error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,
           "oversized settings envelope rejected explicitly");
}

Collected attach_turn(Client& c, std::int64_t dialog) {
  Collected col;
  grpc::ClientContext ctx;
  c.auth(ctx);
  ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(20));
  arli::v1::AttachTurnRequest req;
  req.set_dialog_id(dialog);
  auto reader = c.stub->AttachTurn(&ctx, req);
  arli::v1::TurnEvent ev;
  while (reader && reader->Read(&ev)) {
    col.events.push_back(ev);
  }
  if (reader) {
    col.status = reader->Finish();
  }
  return col;
}

// Drop the UI stream after the first event. The task itself must keep running.
void detach_after_first(Client& c, std::int64_t dialog) {
  grpc::ClientContext ctx;
  c.auth(ctx);
  arli::v1::SendRequest req;
  req.set_dialog_id(dialog);
  req.set_text("detach-me");
  auto reader = c.stub->Send(&ctx, req);
  arli::v1::TurnEvent ev;
  if (reader && reader->Read(&ev)) {
    ctx.TryCancel();
  }
  if (reader) {
    reader->Finish();
  }
}

void t_attach_and_detach(Env& env, Client& c) {
  std::fprintf(stderr, "[attach + ui detach]\n");
  configure(c, env, "slow:3000");
  const auto d = c.create_dialog("attach");
  auto sent = std::async(std::launch::async, [&] { return c.send(d, "slow"); });
  sleep_ms(500);
  const auto live = attach_turn(c, d);
  LT_CHECK(live.status.ok(), "AttachTurn ok");
  LT_CHECK(live.has([](const auto& e) { return e.has_status(); }), "AttachTurn replays steps from the start");
  LT_CHECK(live.done(), "AttachTurn follows the turn through Done");
  LT_CHECK(sent.get().done(), "original Send still finishes");

  const auto d2 = c.create_dialog("detach");
  detach_after_first(c, d2);
  const auto mid = c.last_task(d2);
  LT_CHECK(mid.status() == "running" || mid.status() == "queued", "dropped UI stream does not cancel the task");
  const auto again = attach_turn(c, d2);
  LT_CHECK(again.done() && again.done_text().find("echo: detach-me") != std::string::npos,
           "reattach receives the rest, including the answer");
  LT_CHECK(c.last_task(d2).status() == "done", "task done after reattach");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <arli-daemon> <arli-agent> <arli-mock-llm>\n", argv[0]);
    return 2;
  }
  Env env;
  env.daemon_bin = argv[1];
  env.agent_bin = argv[2];
  env.mock_bin = argv[3];
  char tmpl[] = "/tmp/arli-audit-XXXXXX";
  env.state_dir = mkdtemp(tmpl);
  env.workdir = env.state_dir + "/workspace";
  fs::create_directories(env.workdir);
  const int grpc_port = free_port();
  const int llm_port = free_port();
  env.endpoint = "127.0.0.1:" + std::to_string(grpc_port);
  env.llm_url = "http://127.0.0.1:" + std::to_string(llm_port) + "/v1";
  if (!env.mock.start(env.mock_bin, {std::to_string(llm_port)}, {})) {
    return 2;
  }
  Client c(env.endpoint);
  if (!env.start(c)) {
    std::fprintf(stderr, "daemon did not come up\n");
    env.daemon.term();
    if (env.daemon.wait(5000) < 0) {
      env.daemon.kill9();
    }
    env.mock.kill9();
    return 2;
  }

  t_trace_and_audit(env, c);
  t_temperature(c);
  t_attach_and_detach(env, c);

  env.daemon.term();
  if (env.daemon.wait(8000) < 0) {
    env.daemon.kill9();
  }
  env.mock.kill9();
  fs::remove_all(env.state_dir);
  std::fprintf(stderr, g_failed ? "AUDIT: %d check(s) FAILED\n" : "AUDIT: all checks passed\n", g_failed);
  return g_failed ? 1 : 0;
}
