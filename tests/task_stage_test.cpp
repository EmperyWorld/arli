#include "task_stage.hpp"
#include "durable_context.hpp"
#include "redact.hpp"
#include <iostream>
#include <unistd.h>
using json = nlohmann::json;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
  const json plan = {{"title", "Filesystem API"}, {"checks", {"node tests/fs-test.cjs"}}, {"remaining", {"Explorer"}}};
  arli::TaskStage stage;
  for (const std::string command : {"check_file src/fs.js", "  'check_file' src/fs.js", "true && check_file src/fs.js"}) {
    auto invalid = plan; invalid["checks"] = {command};
    CHECK(!stage.set(invalid).ok && !stage.active());
  }
  for (const std::string command : {"node tests/fs-test.cjs; echo exit=$?", "node tests/fs-test.cjs || true",
                                     "node tests/fs-test.cjs; true", "node tests/fs-test.cjs; printf done"}) {
    auto masked = plan; masked["checks"] = {command};
    CHECK(!stage.set(masked).ok && !stage.active());
  }
  CHECK(!stage.set({{"title", "pretend"}, {"checks", json::array()}, {"remaining", json::array()}}).ok);
  CHECK(stage.set(plan).ok && !stage.passed());
  stage.observe("run_shell", {{"command", "node tests/fs-test.cjs"}}, {true, "ok"}, false);
  CHECK(!stage.passed()); // A claimed/background result cannot satisfy a foreground check.
  stage.observe("run_shell", {{"command", "node tests/fs-test.cjs"}}, {true, "ok"}, true);
  CHECK(stage.passed());
  stage.observe("replace_text", {{"path", "src/fs.js"}}, {true, "SYNTAX_OK:"}, false);
  CHECK(!stage.passed() && stage.code_changed());
  CHECK(!stage.set({{"title", "easier"}, {"checks", {"true"}}, {"remaining", json::array()}}).ok);
  CHECK(stage.final_text("readdir not applied", true, false).find("полное выполнение задачи не подтверждено") != std::string::npos);
  CHECK(stage.final_text("", true, false).find("Explorer") != std::string::npos);
  auto expanded = plan; expanded["remaining"] = {"Notepad", "Explorer"};
  CHECK(stage.set(expanded).ok && stage.state()["remaining"].size() == 2);
  CHECK(stage.set(plan).ok && stage.state()["remaining"].size() == 2); // no silent obligation deletion
  auto too_many = plan; too_many["remaining"] = json::array();
  for (int i = 0; i < 12; ++i) too_many["remaining"].push_back("extra" + std::to_string(i));
  CHECK(!stage.set(too_many).ok && stage.state()["remaining"].size() == 2);
  stage.observe("run_shell", {{"command", "node tests/fs-test.cjs"}}, {true, "ok"}, true);
  stage.observe("run_shell", {{"command", "sed -i changed src/fs.js"}}, {true, "ok"}, true);
  CHECK(!stage.passed());
  stage.observe("run_shell", {{"command", "node tests/fs-test.cjs"}}, {true, "ok"}, true);
  stage.observe("run_shell", {{"command", "false"}}, {false, "exit 1"}, true);
  CHECK(!stage.passed()); // a failing unrelated shell can still have modified files
  for (const auto& malformed : {json::array(), json{{"command", nullptr}}, json{{"command", 42}}}) {
    stage.observe("run_shell", {{"command", "node tests/fs-test.cjs"}}, {true, "ok"}, true);
    stage.observe("run_shell", malformed, {false, "invalid command"}, true);
    CHECK(!stage.passed());
  }
  stage.observe("run_shell", {{"command", "node tests/fs-test.cjs"}}, {true, "ok"}, true);
  stage.observe("start_background", {{"command", "watch-build"}}, {true, "started"}, false);
  CHECK(!stage.passed());
  stage.observe("run_shell", {{"command", "node tests/fs-test.cjs"}}, {true, "ok"}, true);
  CHECK(stage.passed());
  auto saved = stage.state();
  arli::TaskStage resumed(saved);
  CHECK(resumed.active() && !resumed.passed());
  resumed.observe("read_file", {{"path", "src/fs.js"}}, {true, "read"}, false);
  CHECK(resumed.state()["completed"] == true); // read-only exploration does not revoke the previous milestone
  CHECK(resumed.pending().find("node tests/fs-test.cjs") != std::string::npos);
  CHECK(resumed.set({{"title", "Next milestone"}, {"checks", {"node tests/explorer-test.cjs"}}, {"remaining", json::array()}}).ok);
  CHECK(!resumed.defer({{"reason", "   "}}).ok);
  CHECK(resumed.defer({{"reason", "Node unavailable in sandbox"}}).ok);
  CHECK(!resumed.active() && resumed.has_deferred() && resumed.pending().empty());
  auto deferred = resumed.state();
  arli::TaskStage deferred_resume(deferred);
  CHECK(deferred_resume.has_deferred() && !deferred_resume.active());
  CHECK(deferred_resume.final_text("", false, false).find("полное выполнение задачи не подтверждено") != std::string::npos);
  CHECK(deferred_resume.final_text("", false, false).find("Node unavailable") != std::string::npos);
  CHECK(deferred_resume.set(plan).ok);
  deferred_resume.observe("run_shell", {{"command", "node tests/fs-test.cjs"}}, {true, "ok"}, true);
  CHECK(deferred_resume.passed() && deferred_resume.has_deferred()); // another stage cannot erase the deferred obligation
  CHECK(deferred_resume.set({{"title", "Next milestone"}, {"checks", {"node tests/explorer-test.cjs"}}, {"remaining", json::array()}}).ok);
  deferred_resume.observe("run_shell", {{"command", "node tests/explorer-test.cjs"}}, {true, "ok"}, true);
  CHECK(deferred_resume.passed() && !deferred_resume.has_deferred());
  CHECK(!deferred_resume.defer({{"reason", std::string(513, 'x')}}).ok);
  char dir[] = "/tmp/arli-stage-XXXXXX"; CHECK(mkdtemp(dir));
  {
    arli::Database db(std::string(dir) + "/db.sqlite"); db.init();
    const auto id = db.create_dialog("stage").id;
    arli::DurableContext ctx(db, id, dir);
    ctx.save_workflow({{"stage", saved}, {"verification", "{}"}});
    CHECK(arli::DurableContext(db, id, dir).workflow()["stage"] == saved);
    CHECK(arli::DurableContext(db, id, std::string(dir) + "/other").workflow().empty());
    CHECK(db.list_messages(id).empty());
    arli::redact::remember("test-secret-value-98765");
    ctx.save_workflow({{"note", "test-secret-value-98765"}});
    CHECK(db.context_state(id).find("test-secret-value-98765") == std::string::npos);
  }
  std::filesystem::remove_all(dir);
}
