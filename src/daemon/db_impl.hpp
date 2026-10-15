#pragma once

// Internal: shared by db.cpp / db_tasks.cpp. Not part of the public Database API.

#include "db.hpp"

#include <sqlite3.h>

#include <chrono>
#include <ctime>
#include <mutex>
#include <stdexcept>
#include <string>

namespace arli {

// One connection shared by all threads of a process; every public method takes `mu`
// so that step() + last_insert_rowid()/changes() pairs are not interleaved.
struct Database::Impl {
  sqlite3* db = nullptr;
  std::recursive_mutex mu;
};

namespace dbdetail {

using DbLock = std::lock_guard<std::recursive_mutex>;

inline std::string now_iso() {
  using clock = std::chrono::system_clock;
  const auto t = clock::to_time_t(clock::now());
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

inline void check(int rc, sqlite3* db, const char* what) {
  if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
    throw std::runtime_error(std::string(what) + ": " + sqlite3_errmsg(db));
  }
}

inline std::string col_text(sqlite3_stmt* st, int i) {
  const unsigned char* p = sqlite3_column_text(st, i);
  return p ? reinterpret_cast<const char*>(p) : std::string();
}

// RAII prepared statement.
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql, const char* what) : db_(db) {
    check(sqlite3_prepare_v2(db, sql, -1, &st_, nullptr), db, what);
  }
  ~Stmt() {
    if (st_) {
      sqlite3_finalize(st_);
    }
  }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  sqlite3_stmt* get() const { return st_; }
  void bind(int i, std::int64_t v) { sqlite3_bind_int64(st_, i, v); }
  void bind(int i, int v) { sqlite3_bind_int(st_, i, v); }
  void bind(int i, const std::string& v) { sqlite3_bind_text(st_, i, v.c_str(), -1, SQLITE_TRANSIENT); }
  int step() { return sqlite3_step(st_); }
  bool row() { const int rc = step(); check(rc, db_, "read row"); return rc == SQLITE_ROW; }
  void exec(const char* what) { check(step(), db_, what); }

 private:
  sqlite3* db_;
  sqlite3_stmt* st_ = nullptr;
};

inline TaskRow task_from_row(sqlite3_stmt* st) {
  TaskRow row;
  row.id = sqlite3_column_int64(st, 0);
  row.dialog_id = sqlite3_column_int64(st, 1);
  row.turn_id = sqlite3_column_int64(st, 2);
  row.text = col_text(st, 3);
  row.status = col_text(st, 4);
  row.worker_id = col_text(st, 5);
  row.error = col_text(st, 6);
  row.created_at = col_text(st, 7);
  row.updated_at = col_text(st, 8);
  row.finished_at = col_text(st, 9);
  row.config_json = col_text(st, 10);
  return row;
}

inline constexpr const char* kTaskCols =
    "id, dialog_id, turn_id, text, status, worker_id, error, created_at, updated_at, finished_at, config_json";

void create_jobs_table(sqlite3* db);
void create_dialog_page_state(sqlite3* db);

}  // namespace dbdetail
}  // namespace arli
