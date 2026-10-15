#include "durable_context.hpp"
#include "context.hpp"
#include <iostream>
#include <unistd.h>
namespace fs = std::filesystem;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
  char tmp[] = "/tmp/arli-context-XXXXXX"; if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp), file = root / "db.sqlite";
  std::int64_t dialog;
  {
    arli::Database db(file.string()); db.init(); dialog = db.create_dialog("test").id;
    CHECK(db.set_dialog_instructions(dialog, std::string(1200, 'x') + " KEEP_API"));
    CHECK(!db.set_dialog_instructions(dialog, std::string(16385, 'x')));
    db.add_message(dialog, "user", "Keep API");
    arli::DurableContext context(db, dialog, root);
    context.record("write_file", {{"path", "result.txt"}, {"content", "SECRET_OUTPUT"}}, true, 1);
    context.record("run_shell", {{"command", "SECRET_COMMAND"}}, false, 1);
    CHECK(db.list_messages(dialog).size() == 1);
    CHECK(context.render().find("SECRET") == std::string::npos);
  }
  {
    arli::Database db(file.string()); db.init();
    CHECK(db.get_dialog(dialog)->instructions == std::string(1200, 'x') + " KEEP_API");
    CHECK(db.set_dialog_instructions(dialog, ""));
    CHECK(db.get_dialog(dialog)->instructions.empty());
    arli::DurableContext restored(db, dialog, root);
    CHECK(restored.render().find("result.txt") != std::string::npos);
    CHECK(restored.render().find("\"ok\":false") != std::string::npos);
    CHECK(arli::DurableContext(db, dialog, root / "other").render().empty());
    for (int i = 0; i < 100; ++i)
      restored.record("read_file", {{"path", std::to_string(i) + std::string(1000, 'x')}}, true, 2);
    const auto saved = nlohmann::json::parse(db.context_state(dialog));
    CHECK(saved["older_actions_omitted"] == true && saved.dump().size() <= 16384);
    CHECK(saved["recent_actions"].size() <= 32);
    CHECK(db.list_messages(dialog).size() == 1);
    auto messages = nlohmann::json::array({{{"role", "system"}, {"content", restored.render()}}});
    const auto pinned = messages[0];
    for (int i = 0; i < 30; ++i) messages.push_back({{"role", "user"}, {"content", std::string(2000, 'x')}});
    arli::context::fit_budget(messages, 8192);
    CHECK(messages[0] == pinned);
    db.save_context_state(dialog, "{broken");
    CHECK(arli::DurableContext(db, dialog, root).render().empty());
    db.save_context_state(dialog, "{\"version\": []}");
    CHECK(arli::DurableContext(db, dialog, root).render().empty());
    CHECK(db.delete_dialog(dialog));
    CHECK(db.context_state(dialog).empty());
  }
  fs::remove_all(root);
}
