// Real worker turn + deterministic HTTP model: rejected edit is not a source check.
#include "lifecycle_harness.hpp"
#include "turn.hpp"
#include "durable_context.hpp"
#include "verification.hpp"
#include <fstream>

using namespace lt;
namespace fs = std::filesystem;
using nlohmann::json;
namespace {
bool ready(int port) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port)); addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool connected = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    close(fd); if (connected) return true;
    sleep_ms(20);
  }
  return false;
}
json source(const fs::path& path) { std::ifstream file(path); json result; file >> result; return result; }
bool rejected(const Collected& events) {
  return events.has([](const auto& event) { return event.has_observation() && !event.observation().ok() &&
      event.observation().output().find("SYNTAX_REJECTED:") != std::string::npos; });
}
bool checked(const Collected& events) {
  return events.has([](const auto& event) { return event.has_observation() && event.observation().ok() &&
      event.observation().tool() == "check_file"; });
}
bool failed(const Collected& events) {
  return events.has([](const auto& event) { return event.has_done() && event.done().outcome() == "failed"; });
}
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  char temp[] = "/tmp/arli-turn-recovery-XXXXXX";
  if (!mkdtemp(temp)) return 2;
  setenv("ARLI_STATE_DIR", temp, 1);
  const fs::path root(temp), workdir = root / "workspace", database = root / "state.sqlite";
  fs::create_directories(workdir);
  Proc mock;
  const int port = free_port();
  if (!mock.start(argv[1], {std::to_string(port)}, {}) || !ready(port)) {
    mock.kill9(); fs::remove_all(root); return 2;
  }
  arli::TurnConfig cfg;
  cfg.workdir = workdir.string(); cfg.max_steps = 16;
  cfg.llm_base_url = "http://127.0.0.1:" + std::to_string(port) + "/v1";
  auto run = [&](std::int64_t dialog, std::int64_t turn, const std::string& model, const std::string& prompt,
                 bool cancel_after_edit = false) {
    // New database connection and worker-local state for every turn.
    arli::Database db(database.string()); db.init();
    cfg.llm_model = model;
    Collected events;
    std::atomic<bool> cancel{false}; arli::TurnControl ctl; ctl.cancel = &cancel;
    arli::run_turn(db, cfg, dialog, prompt, turn, [&](const auto& event) {
      events.events.push_back(event);
      if (cancel_after_edit && event.has_observation() && event.observation().ok() && event.observation().tool() == "replace_text")
        cancel = true;
      return true;
    }, ctl);
    return events;
  };
  auto durable = [&](std::int64_t dialog) {
    arli::Database db(database.string()); db.init();
    arli::DurableContext context(db, dialog, workdir);
    arli::VerificationState state;
    LT_CHECK(state.restore(context.workflow().value("verification", std::string())), "durable verification state decodes");
    return state;
  };
  for (const std::string model : {"recovery_stuck", "recovery_unrelated", "recovery_abandon"}) {
    std::ofstream(workdir / "config.json") << "{\"value\":1,\"label\":\"old\"}\n";
    std::int64_t dialog = 0;
    { arli::Database db(database.string()); db.init(); dialog = db.create_dialog(model).id; }
    const auto events = run(dialog, 1, model, "Исправь config.json: value должна стать 2");
    LT_CHECK(rejected(events) && checked(events), "actual rejected edit followed by successful old-file check reproduced");
    LT_CHECK(source(workdir / "config.json").at("value") == 1, "rejected requested value was never written");
    if (model != "recovery_abandon") LT_CHECK(failed(events), "unresolved rejected change prevents a successful turn");
    LT_CHECK(events.done_text().find("FALSE_SUCCESS") == std::string::npos, "model's false success is removed from final result");
    const auto state = durable(dialog);
    if (model == "recovery_abandon") {
      LT_CHECK(state.has_abandoned() && state.pending().empty(), "explicit abandoned operation survives restart as incomplete");
      LT_CHECK(events.has([](const auto& event) { return event.has_done() && event.done().verification_state() == "partial"; }),
               "abandoned operation is reported as partial verification");
      LT_CHECK(events.done_text().find("отложенные правки") != std::string::npos, "partial final explicitly reports abandoned work");
      continue;
    }
    LT_CHECK(!state.pending().empty(), "actual rejected candidate persists beyond worker turn");
    if (model == "recovery_unrelated") {
      LT_CHECK(source(workdir / "config.json").at("label") == "new", "unrelated same-file mutation actually succeeded");
      continue;
    }
    const auto second = run(dialog, 2, "recovery_check_only", "Продолжаем");
    LT_CHECK(checked(second) && failed(second), "continuation cannot clear rejected edit merely by checking old file");
    LT_CHECK(second.done_text().find("FALSE_SUCCESS") == std::string::npos && !durable(dialog).pending().empty(),
             "false completion blocked after context reload");
    const auto third = run(dialog, 3, "recovery_fix", "Продолжаем, исправь value");
    LT_CHECK(source(workdir / "config.json").at("value") == 2 && checked(third), "fresh continuation actually repairs and checks requested fragment");
    LT_CHECK(third.has([](const auto& event) { return event.has_done() && event.done().outcome() == "done"; }),
             "real correction can complete normally");
    LT_CHECK(durable(dialog).pending().empty(), "corresponding correction clears durable rejected target");
  }
  cfg.shell_auto = "sh";
  for (const std::string model : {"recovery_code_failed", "recovery_code_cancelled", "recovery_code_limit", "recovery_shell_invalidates"}) {
    std::ofstream(workdir / "module.js") << "export const value = 1;\n";
    std::ofstream(workdir / "verify.sh") << (model == "recovery_shell_invalidates" ? "exit 0\n" : "if then # malformed\n");
    std::ofstream(workdir / "mutate.sh") << "printf changed > mutation.flag\nexit 7\n";
    std::int64_t dialog = 0;
    { arli::Database db(database.string()); db.init(); dialog = db.create_dialog(model).id; }
    cfg.max_steps = model == "recovery_code_limit" ? 3 : 16;
    const auto events = run(dialog, 1, model, "Исправь module.js и выполни проверки подэтапа", model == "recovery_code_cancelled");
    LT_CHECK(events.done_text().find("FALSE_SUCCESS") == std::string::npos, "termination never retains model's unsupported success");
    if (model == "recovery_code_cancelled") {
      LT_CHECK(events.has([](const auto& event) { return event.has_done() && event.done().outcome() == "cancelled"; }), "cancelled code turn retains lifecycle outcome");
      LT_CHECK(events.done_text().find("отменён") != std::string::npos, "cancel reason survives deterministic stage final formatting");
    } else {
      LT_CHECK(failed(events), "failed validation, limit, or invalidated check cannot complete code stage");
      if (model == "recovery_code_limit")
        LT_CHECK(events.done_text().find("лимит шагов") != std::string::npos, "step limit reason survives deterministic stage final formatting");
      if (model == "recovery_code_failed")
        LT_CHECK(events.done_text().find("Проверка не прошла") != std::string::npos && events.done_text().find("sh -n verify.sh") != std::string::npos,
                 "actual failed check remains visible after code-stage formatting");
    }
    if (model == "recovery_shell_invalidates") {
      LT_CHECK(fs::exists(workdir / "mutation.flag"), "unrelated shell command actually mutated workspace before failing");
      LT_CHECK(events.has([](const auto& event) { return event.has_observation() && event.observation().tool() == "run_shell" && event.observation().ok(); }),
               "stage check genuinely passed before unrelated shell mutation");
      LT_CHECK(events.done_text().find("Не выполнена проверка подэтапа") != std::string::npos,
               "even failing unrelated shell mutation invalidates previously passed stage check");
    } else {
      std::ifstream code(workdir / "module.js"); const std::string bytes{std::istreambuf_iterator<char>(code), {}};
      LT_CHECK(bytes.find("value = 2") != std::string::npos, "termination scenario actually changed a source-code file");
    }
    arli::Database db(database.string()); db.init(); arli::DurableContext context(db, dialog, workdir);
    LT_CHECK(context.workflow().at("stage").contains("checks"), "interrupted/failed stage remains persisted for continuation");
  }
  mock.kill9(); fs::remove_all(root);
  return g_failed ? 1 : 0;
}
