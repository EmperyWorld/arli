// Full worker turn against a deterministic mock model; no live LLM or UI needed.
#include "lifecycle_harness.hpp"
#include "turn.hpp"
#include "file_pass.hpp"
#include "turn_policy.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace lt;
namespace fs = std::filesystem;

std::string read_text(const fs::path& file) {
  std::ifstream input(file);
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

bool unverified_done(const Collected& events) {
  return events.done_text() == "Автоматическая проверка результата неполная: Mock task has no declared file criteria\ndone" &&
      events.has([](const auto& e) { return e.has_done() && e.done().outcome() == "done" &&
          e.done().verification_state() == "unverified"; });
}

int main(int argc, char** argv) {
  LT_CHECK(arli::classify_turn_intent("Давай улучшим наше веб приложение").requires_tools,
           "the actual user improvement request requires tools");
  LT_CHECK(!arli::classify_turn_intent("Расскажи, как улучшить приложение").expects_mutation,
           "advice is still not an edit request");
  LT_CHECK(arli::classify_turn_intent("А остальные файлы?\nработаем").expects_mutation,
           "actual continuation request is actionable");
  LT_CHECK(arli::classify_turn_intent("1", "1. Дописать/исправить логику в script.js\n2. Только объясни код").expects_mutation,
           "choice resolves to the offered edit");
  LT_CHECK(!arli::classify_turn_intent("2", "1. Исправить script.js\n2. Только объясни код").expects_mutation,
           "read-only choice is not treated as an edit");
  LT_CHECK(!arli::classify_turn_intent("1").expects_mutation, "bare number does not invent a task");
  LT_CHECK(arli::classify_turn_intent("Привет. Давай улучшим проект").needs_clarification,
           "vague project improvement requires clarification before edits");
  LT_CHECK(!arli::classify_turn_intent("Улучши обработку ошибок gRPC").needs_clarification,
           "concrete improvement remains actionable");
  if (argc != 2) return 2;
  char tmp[] = "/tmp/arli-turn-execution-XXXXXX";
  const char* root = mkdtemp(tmp);
  if (!root) return 2;
  setenv("ARLI_STATE_DIR", root, 1);
  const fs::path workdir = fs::path(root) / "workspace";
  fs::create_directories(workdir);
  const int port = free_port();
  Proc mock;
  if (!mock.start(argv[1], {std::to_string(port)}, {})) return 2;
  // Wait for the mock listener, without issuing a completion request.
  bool ready = false;
  for (int i = 0; i < 100 && !ready; ++i) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ready = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    close(fd);
    if (!ready) sleep_ms(20);
  }
  if (!ready) { mock.kill9(); fs::remove_all(root); return 2; }
  {
    arli::Database db((fs::path(root) / "test.sqlite").string());
    db.init();
    arli::TurnConfig cfg;
    cfg.workdir = workdir.string();
    cfg.llm_base_url = "http://127.0.0.1:" + std::to_string(port) + "/v1";
    cfg.max_steps = 16;
    for (const std::string model : {"edit_once", "premature_edit", "append_many", "echo"}) {
      {
        std::ofstream seed(workdir / "target.txt");
        seed << "old\n2\n3\n4\n5\n6\n7\n";
      }
      cfg.llm_model = model;
      Collected events;
      const auto dialog = db.create_dialog(model);
      arli::run_turn(db, cfg, dialog.id, "исправь target.txt", 1,
                    [&](const auto& event) { events.events.push_back(event); return true; }, {});
      int reads = 0, writes = 0, failures = 0;
      for (const auto& e : events.events) {
        if (!e.has_observation()) continue;
        failures += !e.observation().ok();
        reads += e.observation().tool() == "read_file";
        writes += e.observation().ok() &&
                  (e.observation().tool() == "edit_file" || e.observation().tool() == "append_file");
      }
      LT_CHECK(events.done(), "turn finishes");
      LT_CHECK(failures == 0, "no spurious tool failures");
      if (model == "echo") {
        LT_CHECK(writes == 0, "text-only model did not change files");
        LT_CHECK(events.done_text().find("Файлы в этом ходе не изменены") != std::string::npos,
                 "text-only failure is explicit");
      } else if (model == "append_many") {
        LT_CHECK(writes == 4, "four distinct small appends are executed");
        LT_CHECK(read_text(workdir / "target.txt").find("part0\npart1\npart2\npart3\n") != std::string::npos,
                 "all append chunks are on disk");
      } else {
        LT_CHECK(reads == 7 && writes == 1, "one sufficient edit finishes without write quota");
        LT_CHECK(read_text(workdir / "target.txt") == "fixed\n2\n3\n4\n5\n6\n7\n", "actual file edited");
        LT_CHECK(unverified_done(events), "model final answer retained");
      }
    }
    for (const std::string model : {"recover_args", "review_queue", "readonly_attempt", "copy_success", "long_mock", "repair_history"}) {
      std::string original = "old\n2\n3\n4\n5\n6\n7\n";
      if (model == "long_mock") {
        original.clear();
        for (int i = 1; i <= 10; ++i) original += std::to_string(i) + " KEEP\n";
        for (int i = 0; i < 100; ++i) original += std::string(100, 'x') + "\n";
      }
      std::ofstream(workdir / "target.txt") << original;
      std::ofstream(workdir / "dependency.txt") << "correct\n";
      cfg.llm_model = model;
      cfg.max_steps = 32;
      cfg.context_tokens = 16384;
      Collected events;
      const auto dialog = db.create_dialog(model);
      const std::string prompt = model == "readonly_attempt" ? "Проверь target.txt, не редактируй файлы" :
                                 model == "review_queue" ? "Проверь файлы target.txt и dependency.txt" :
                                 model == "copy_success" ? "Скопируй файл target.txt в copy.txt" :
                                 "Исправь target.txt. KEEP_CONSTRAINT: сохраняй остальное";
      arli::run_turn(db, cfg, dialog.id, prompt, 1, [&](const auto& e) {
        events.events.push_back(e);
        // The test sink performs the orchestrator's persistence role.
        if (e.has_file_pass()) {
          const auto& p = e.file_pass();
          db.set_file_pass(dialog.id, {p.paths().begin(), p.paths().end()}, {p.done().begin(), p.done().end()},
                           {p.reviewed().begin(), p.reviewed().end()});
        }
        return true;
      }, {});
      LT_CHECK(events.done(), "robust turn finishes");
      if (model == "recover_args") {
        LT_CHECK(events.has([](const auto& e) { return e.has_observation() && !e.observation().ok(); }), "invalid args reported");
        LT_CHECK(read_text(workdir / "target.txt") == "fixed\n2\n3\n4\n5\n6\n7\n", "model recovers after rejected args");
      } else if (model == "review_queue") {
        const auto row = db.get_file_pass(dialog.id);
        LT_CHECK(row && row->reviewed.size() == 2 && row->done.empty(), "review state persisted without claiming writes");
        LT_CHECK(read_text(workdir / "target.txt") == original, "inspection leaves files unchanged");
        LT_CHECK(row && arli::FilePassState::from_row(*row).head().empty(), "restored queue completed");
      } else if (model == "readonly_attempt") {
        LT_CHECK(read_text(workdir / "target.txt") == original, "explicit no-edit instruction enforced");
        LT_CHECK(events.has([](const auto& e) { return e.has_observation() && !e.observation().ok(); }), "write denied");
      } else if (model == "repair_history") {
        LT_CHECK(read_text(workdir / "target.txt") == original + "literal ,}", "JSON repair preserves literal content on disk");
        LT_CHECK(unverified_done(events), "repaired arguments preserved for next model round");
      } else if (model == "copy_success") {
        LT_CHECK(read_text(workdir / "copy.txt") == original, "copy completed");
        LT_CHECK(unverified_done(events), "copy counted as actual mutation");
      } else {
        std::string expected;
        for (int i = 1; i <= 10; ++i) expected += std::to_string(i) + " DONE\n";
        for (int i = 0; i < 100; ++i) expected += std::string(100, 'x') + "\n";
        LT_CHECK(read_text(workdir / "target.txt") == expected, "long task completed with surrounding content preserved");
        LT_CHECK(events.has([](const auto& e) { return e.has_context_use() && e.context_use().compressed(); }), "compaction exercised");
        LT_CHECK(unverified_done(events), "long task retains goal");
      }
    }
    for (const std::string model : {"raw_calls", "json_actions", "raw_calls_stuck"}) {
      std::ofstream(workdir / "target.txt") << (model == "json_actions" ? "old value\n" : "old\n");
      cfg.llm_model = model;
      cfg.llm_profile = "gemma4";
      const auto dialog = db.create_dialog(model);
      db.add_message(dialog.id, "assistant", "<call:list_dir{path:<|\"|>.<|\"|>}/>", 0);
      Collected result;
      arli::run_turn(db, cfg, dialog.id, "Исправь target.txt", 1,
                    [&](const auto& e) { result.events.push_back(e); return true; }, {});
      if (model != "raw_calls_stuck") {
        LT_CHECK(read_text(workdir / "target.txt") == "fixed\n", "raw call repaired into a real edit");
        LT_CHECK(unverified_done(result), "repair resumes task normally");
      } else {
        LT_CHECK(read_text(workdir / "target.txt") == "old\n", "raw text never dispatched");
        LT_CHECK(result.error_contains("не исполнен"), "unrepairable format reported explicitly");
      }
    }
    for (const std::string model : {"unread_edit", "unread_range", "write_twice"}) {
      std::ofstream(workdir / "target.txt") << "old\nsecond\n";
      cfg.llm_model = model;
      Collected result; const auto dialog = db.create_dialog(model);
      arli::run_turn(db, cfg, dialog.id, "Исправь target.txt", 1,
                    [&](const auto& e) { result.events.push_back(e); return true; }, {});
      LT_CHECK(read_text(workdir / "target.txt") == (model == "write_twice" ? "fixed\nsecond\n" : "old\nsecond\n"),
               "unread or invalidated lines never change");
      LT_CHECK(result.has([](const auto& e) { return e.has_observation() && !e.observation().ok() &&
          e.observation().output().find("READ_REQUIRED") != std::string::npos; }), "read gate enforced by real worker turn");
    }
    for (const std::string model : {"failed_check", "check_stuck"}) {
      std::ofstream(workdir / "target.sh") << "if then # broken\n";
      cfg.llm_model = model; cfg.shell_auto = "/bin/sh";
      Collected result; const auto dialog = db.create_dialog(model);
      arli::run_turn(db, cfg, dialog.id, "Исправь target.sh и проверь синтаксис", 1,
                    [&](const auto& e) { result.events.push_back(e); return true; }, {});
      if (model == "failed_check") {
        LT_CHECK(read_text(workdir / "target.sh") == ": # fixed\n", "actual failed shell check recovered");
        int successful = 0;
        for (const auto& e : result.events)
          successful += e.has_observation() && e.observation().tool() == "run_shell" && e.observation().ok();
        LT_CHECK(successful == 1, "premature finish forces a real successful recheck");
      } else {
        LT_CHECK(result.error_contains("Проверка не прошла"), "failed verification cannot be called successful");
        LT_CHECK(result.done_text().find("Я всё исправила") == std::string::npos, "false success removed from final answer");
      }
    }
    for (const std::string model : {"syntax_repair", "syntax_stuck", "syntax_inferred"}) {
      std::ofstream(workdir / "config.json") << "{\"value\":1}\n";
      cfg.llm_model = model;
      Collected result; const auto dialog = db.create_dialog(model);
      arli::run_turn(db, cfg, dialog.id, "Исправь config.json: value должна быть 2", 1,
                    [&](const auto& e) { result.events.push_back(e); return true; }, {});
      LT_CHECK(result.has([](const auto& e) { return e.has_observation() && !e.observation().ok() &&
          e.observation().output().find("SYNTAX_REJECTED") != std::string::npos; }), "worker reports rejected candidate");
      if (model == "syntax_repair") {
        LT_CHECK(read_text(workdir / "config.json") == "{\"value\":2}\n", "premature final resumes and repairs invalid candidate");
        LT_CHECK(!result.error_contains("Проверка не прошла"), "validated correction clears failure");
      } else {
        LT_CHECK(read_text(workdir / "config.json") == "{\"value\":1}\n", "stuck model cannot corrupt the file");
        LT_CHECK(result.error_contains("Проверка не прошла"), "failed write cannot be reported as success");
        LT_CHECK(result.done_text().find("Я всё исправила") == std::string::npos, "false success removed");
      }
    }
    fs::create_directory(workdir / "nested");
    std::ofstream(workdir / "AGENTS.md") << "ROOT_RULE";
    std::ofstream(workdir / "nested/AGENTS.md") << "NESTED_RULE";
    cfg.llm_model = "scoped_rules";
    Collected rules_events;
    const auto rules_dialog = db.create_dialog("scoped rules");
    arli::run_turn(db, cfg, rules_dialog.id, "Создай nested/result.txt", 1,
                  [&](const auto& e) { rules_events.events.push_back(e); return true; }, {});
    LT_CHECK(read_text(workdir / "nested/result.txt") == "rules applied", "rules loaded before mutation");
    LT_CHECK(rules_events.has([](const auto& e) { return e.has_observation() && !e.observation().ok() &&
        e.observation().output().find("RULES_UPDATED") != std::string::npos; }), "first uninformed mutation blocked");
    fs::remove(workdir / "AGENTS.md"); fs::remove_all(workdir / "nested");
    cfg.dialog_instructions = std::string(1400, 'x') + " END_PINNED_RULE";
    cfg.llm_model = "pinned_rules";
    const auto pinned_dialog = db.create_dialog("pinned rules");
    for (int i = 0; i < 50; ++i) {
      db.add_message(pinned_dialog.id, "user", std::string(1000, 'u'));
      db.add_message(pinned_dialog.id, "assistant", std::string(1000, 'a'));
    }
    arli::run_turn(db, cfg, pinned_dialog.id, "Создай pinned.txt по закреплённым правилам", 1,
                  [](const auto&) { return true; }, {});
    LT_CHECK(read_text(workdir / "pinned.txt") == "pinned applied", "full pinned rule reaches model");
    fs::remove(workdir / "pinned.txt"); cfg.dialog_instructions.clear();
    for (const auto* model : {"acceptance_repair", "acceptance_stuck"}) {
      std::ofstream(workdir / "acceptance.txt") << "1 KEEP\n2 KEEP\n3 KEEP\n";
      cfg.llm_model = model; Collected result;
      const auto dialog = db.create_dialog(model);
      bool plan_before_action=false;
      arli::run_turn(db, cfg, dialog.id, "Измени acceptance.txt: все три строки должны стать DONE", 1,
                    [&](const auto& e) {
                      if(e.has_action()&&!plan_before_action)for(const auto& file:fs::recursive_directory_iterator(fs::path(root)/"changesets"))if(file.path().filename()=="manifest.json"){
                        const auto record=nlohmann::json::parse(read_text(file.path()));if(record["dialog_id"]==dialog.id)
                          plan_before_action=record["entries"].empty()&&record["acceptance_plan"]["checks"][0]["end_line"]==3;
                      }
                      result.events.push_back(e); return true;
                    }, {});
      LT_CHECK(plan_before_action,"production turn durably saves criteria before first tool action");
      const bool repaired = std::string(model) == "acceptance_repair";
      LT_CHECK((read_text(workdir / "acceptance.txt") == "1 DONE\n2 DONE\n3 DONE\n") == repaired, "remaining task criterion forces actual repair");
      LT_CHECK(result.has([&](const auto& e) { return e.has_done() &&
          e.done().outcome() == (repaired ? "done" : "failed") &&
          e.done().verification_state() == (repaired ? "checks_passed" : "failed") &&
          !e.done().verification_plan_json().empty(); }), "terminal result reports independently checked plan");
    }
    fs::remove(workdir / "acceptance.txt");
    cfg.llm_model = "scoped_create";
    Collected scoped;
    const auto scoped_dialog = db.create_dialog("scoped creation");
    arli::run_turn(db, cfg, scoped_dialog.id, "Создай result.txt. Других файлов не создавай.", 1,
                  [&](const auto& e) { scoped.events.push_back(e); return true; }, {});
    LT_CHECK(read_text(workdir / "result.txt") == "ready", "explicit creation executed");
    LT_CHECK(!fs::exists(workdir / "other.txt") && !fs::exists(workdir / "copy-result.txt"),
             "unrequested creations denied in actual worker");
    int denied = 0;
    for (const auto& e : scoped.events)
      denied += e.has_observation() && !e.observation().ok() &&
                e.observation().output().find("CREATE_FORBIDDEN") != std::string::npos;
    LT_CHECK(denied == 2 && scoped.done(), "both forbidden attempts reported and turn finishes");
    cfg.shell_auto.clear();
    cfg.llm_model = "append_many";
    cfg.max_steps = 1;
    cfg.context_tokens = 16384;
    Collected limited;
    const auto limited_dialog = db.create_dialog("limit");
    arli::run_turn(db, cfg, limited_dialog.id, "добавь строки в target.txt", 1,
                  [&](const auto& e) { limited.events.push_back(e); return true; }, {});
    LT_CHECK(limited.error_contains("лимит шагов"), "step exhaustion is reported as incomplete");
    LT_CHECK(limited.done_text().find("может быть не завершена") != std::string::npos, "partial writes not called completion");
    LT_CHECK(!fs::exists(workdir / "index.html"), "no invented web project");
  }
  mock.kill9();
  fs::remove_all(root);
  return g_failed ? 1 : 0;
}
