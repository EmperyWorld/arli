#include "db.hpp"
#include "file_pass.hpp"
#include <sqlite3.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
int main() {
  char tmp[] = "/tmp/arli-db-file-pass-XXXXXX";
  const char* root = mkdtemp(tmp);
  CHECK(root);
  setenv("ARLI_STATE_DIR", root, 1);
  const auto path = std::filesystem::path(root) / "legacy.sqlite";
  sqlite3* legacy = nullptr;
  CHECK(sqlite3_open(path.c_str(), &legacy) == SQLITE_OK);
  // The old v6 queue must survive the additive migration unchanged.
  CHECK(sqlite3_exec(legacy,
    "CREATE TABLE schema_version(version INTEGER NOT NULL); INSERT INTO schema_version VALUES(6);"
    "CREATE TABLE file_pass(dialog_id INTEGER PRIMARY KEY, paths_json TEXT NOT NULL DEFAULT '[]',"
    "done_json TEXT NOT NULL DEFAULT '[]', updated_at TEXT NOT NULL DEFAULT '');"
    "INSERT INTO file_pass VALUES(1,'[\"a.txt\",\"b.txt\"]','[\"a.txt\"]','old');",
    nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(legacy);
  {
    arli::Database db(path.string());
    db.init();
    db.init();
    CHECK(db.schema_version() >= 7);
    const auto old = db.get_file_pass(1);
    CHECK(old && old->paths.size() == 2 && old->done.size() == 1 && old->reviewed.empty());
    db.set_file_pass(1, old->paths, old->done, {"b.txt"});
  }
  {
    arli::Database reopened(path.string());
    reopened.init();
    const auto row = reopened.get_file_pass(1);
    CHECK(row && row->done == std::vector<std::string>{"a.txt"});
    CHECK(row->reviewed == std::vector<std::string>{"b.txt"});
    CHECK(arli::FilePassState::from_row(*row).head().empty());
    reopened.clear_file_pass(1);
    CHECK(!reopened.get_file_pass(1));
  }
  std::filesystem::remove_all(root);
  return 0;
}
