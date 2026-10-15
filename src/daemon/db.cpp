#include "db.hpp"

#include "db_impl.hpp"
#include "paths.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

#include <sys/stat.h>

namespace arli {

using namespace dbdetail;

namespace {

bool column_exists(sqlite3* db, const char* table, const char* column) {
  const std::string sql = std::string("PRAGMA table_info(") + table + ")";
  Stmt st(db, sql.c_str(), "pragma table_info");
  while (st.row()) {
    if (col_text(st.get(), 1) == column) {
      return true;
    }
  }
  return false;
}

void exec_sql(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "sqlite exec failed";
    sqlite3_free(err);
    throw std::runtime_error(msg);
  }
}

void add_column_if_missing(sqlite3* db, const char* table, const char* column, const char* decl) {
  if (column_exists(db, table, column)) {
    return;
  }
  const std::string sql = std::string("ALTER TABLE ") + table + " ADD COLUMN " + column + " " + decl;
  exec_sql(db, sql.c_str());
}

int read_schema_version(sqlite3* db) {
  Stmt st(db, "SELECT version FROM schema_version LIMIT 1", "schema version");
  if (st.row()) {
    return sqlite3_column_int(st.get(), 0);
  }
  return 0;
}

void write_schema_version(sqlite3* db, int v) {
  exec_sql(db, "DELETE FROM schema_version");
  Stmt st(db, "INSERT INTO schema_version(version) VALUES(?1)", "set schema version");
  st.bind(1, v);
  st.exec("set schema version");
}

}  // namespace

Database::Database(std::string path) : impl_(new Impl) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(path.c_str(), &impl_->db, flags, nullptr) != SQLITE_OK) {
    throw std::runtime_error("sqlite open failed");
  }
  // One user, one machine: the database holds the LLM key.
  chmod(path.c_str(), 0600);
  sqlite3_exec(impl_->db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
  sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_busy_timeout(impl_->db, 5000);
}

Database::~Database() {
  if (impl_) {
    if (impl_->db) {
      sqlite3_close(impl_->db);
    }
    delete impl_;
  }
}

// Schema history:
//   1 — M3.x: settings, dialogs, messages, tasks(status queued|running|done|failed)
//   2 — Э2: tasks + error/updated_at/finished_at; status cancelled
//   3 — Э3: steps (persistent trace), messages.turn_id
void Database::init() {
  DbLock lock(impl_->mu);
  sqlite3* db = impl_->db;
  exec_sql(db, R"sql(
CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS dialogs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  title TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dialog_id INTEGER NOT NULL REFERENCES dialogs(id) ON DELETE CASCADE,
  role TEXT NOT NULL,
  text TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS tasks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dialog_id INTEGER NOT NULL REFERENCES dialogs(id) ON DELETE CASCADE,
  turn_id INTEGER NOT NULL,
  text TEXT NOT NULL,
  status TEXT NOT NULL,
  worker_id TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS steps (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dialog_id INTEGER NOT NULL,
  turn_id INTEGER NOT NULL,
  task_id INTEGER NOT NULL,
  step_id INTEGER NOT NULL DEFAULT 0,
  kind TEXT NOT NULL,
  tool TEXT NOT NULL DEFAULT '',
  args_json TEXT NOT NULL DEFAULT '',
  ok INTEGER NOT NULL DEFAULT 1,
  output TEXT NOT NULL DEFAULT '',
  diff TEXT NOT NULL DEFAULT '',
  duration_ms INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);
)sql");

  int version = read_schema_version(db);
  if (version < 2) {
    add_column_if_missing(db, "tasks", "error", "TEXT NOT NULL DEFAULT ''");
    add_column_if_missing(db, "tasks", "updated_at", "TEXT NOT NULL DEFAULT ''");
    add_column_if_missing(db, "tasks", "finished_at", "TEXT NOT NULL DEFAULT ''");
    exec_sql(db, "UPDATE tasks SET updated_at = created_at WHERE updated_at = ''");
    exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_tasks_status ON tasks(status, id)");
    exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_tasks_dialog ON tasks(dialog_id, id)");
    version = 2;
    write_schema_version(db, version);
  }
  if (version < 3) {
    exec_sql(db, R"sql(
CREATE TABLE IF NOT EXISTS steps (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dialog_id INTEGER NOT NULL,
  turn_id INTEGER NOT NULL,
  task_id INTEGER NOT NULL,
  step_id INTEGER NOT NULL DEFAULT 0,
  kind TEXT NOT NULL,
  tool TEXT NOT NULL DEFAULT '',
  args_json TEXT NOT NULL DEFAULT '',
  ok INTEGER NOT NULL DEFAULT 1,
  output TEXT NOT NULL DEFAULT '',
  duration_ms INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_steps_task ON steps(task_id, id);
CREATE INDEX IF NOT EXISTS idx_steps_turn ON steps(dialog_id, turn_id, id);
)sql");
    add_column_if_missing(db, "messages", "turn_id", "INTEGER NOT NULL DEFAULT 0");
    version = 3;
    write_schema_version(db, version);
  }
  if (version < 4) {
    exec_sql(db, R"sql(
CREATE TABLE IF NOT EXISTS file_pass (
  dialog_id INTEGER PRIMARY KEY REFERENCES dialogs(id) ON DELETE CASCADE,
  paths_json TEXT NOT NULL DEFAULT '[]',
  done_json TEXT NOT NULL DEFAULT '[]',
  updated_at TEXT NOT NULL DEFAULT ''
);
)sql");
    version = 4;
    write_schema_version(db, version);
  }
  if (version < 5) {
    exec_sql(db, R"sql(
CREATE TABLE IF NOT EXISTS plans (
  dialog_id INTEGER PRIMARY KEY REFERENCES dialogs(id) ON DELETE CASCADE,
  steps_json TEXT NOT NULL DEFAULT '[]',
  current INTEGER NOT NULL DEFAULT 0,
  failed INTEGER NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS memory (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  text TEXT NOT NULL,
  created_at TEXT NOT NULL
);
)sql");
    version = 5;
    write_schema_version(db, version);
  }
  if (version < 6) {
    add_column_if_missing(db, "steps", "diff", "TEXT NOT NULL DEFAULT ''");
    version = 6;
    write_schema_version(db, version);
  }
  if (version < 7) {
    add_column_if_missing(db, "file_pass", "reviewed_json", "TEXT NOT NULL DEFAULT '[]'");
    version = 7;
    write_schema_version(db, version);
  }
  if (version < 8) {
    add_column_if_missing(db, "tasks", "config_json", "TEXT NOT NULL DEFAULT ''");
    version = 8; write_schema_version(db, version);
  }
  if (version < 9) {
    add_column_if_missing(db, "tasks", "request_id", "TEXT NOT NULL DEFAULT ''");
    exec_sql(db, "CREATE UNIQUE INDEX IF NOT EXISTS task_request ON tasks(dialog_id,request_id) WHERE request_id<>''");
    exec_sql(db, "CREATE TABLE IF NOT EXISTS events(id INTEGER PRIMARY KEY AUTOINCREMENT, task_id INTEGER NOT NULL REFERENCES tasks(id) ON DELETE CASCADE, payload TEXT NOT NULL)");
    exec_sql(db, "CREATE INDEX IF NOT EXISTS events_task ON events(task_id,id)");
    version = 9; write_schema_version(db, version);
  }
  if (version < 10) {
    exec_sql(db, "CREATE TABLE IF NOT EXISTS command_receipts(id INTEGER PRIMARY KEY AUTOINCREMENT, task_id INTEGER NOT NULL REFERENCES tasks(id) ON DELETE CASCADE, status TEXT NOT NULL DEFAULT 'pending')");
    version = 10; write_schema_version(db, version);
  }
  if (version < 11) {
    add_column_if_missing(db, "dialogs", "instructions", "TEXT NOT NULL DEFAULT ''");
    version = 11; write_schema_version(db, version);
  }
  dbdetail::create_jobs_table(db);
  if (version < 12) {
    add_column_if_missing(db, "jobs", "process_identity", "TEXT NOT NULL DEFAULT ''");
    add_column_if_missing(db, "jobs", "exit_code", "INTEGER NOT NULL DEFAULT -1");
    version = 12; write_schema_version(db, version);
  }
  std::filesystem::create_directories(state_root() / "workspace");
  if (version < 13) {
    add_column_if_missing(db, "jobs", "dialog_id", "INTEGER NOT NULL DEFAULT 0");
    add_column_if_missing(db, "jobs", "task_id", "INTEGER NOT NULL DEFAULT 0");
    add_column_if_missing(db, "jobs", "turn_id", "INTEGER NOT NULL DEFAULT 0");
    exec_sql(db, "CREATE INDEX IF NOT EXISTS jobs_owner ON jobs(dialog_id,task_id,id)");
    version = 13; write_schema_version(db, version);
  }
  if(version<14){
    add_column_if_missing(db,"jobs","finished_at","TEXT NOT NULL DEFAULT ''");
    exec_sql(db,"UPDATE jobs SET finished_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE status!='running' AND finished_at=''");
    version=14;write_schema_version(db,version);
  }
  exec_sql(db,"CREATE INDEX IF NOT EXISTS jobs_log_path ON jobs(log_path)");
  exec_sql(db,"CREATE INDEX IF NOT EXISTS jobs_log_suffix ON jobs(substr(log_path,-21))");
  exec_sql(db,"CREATE INDEX IF NOT EXISTS jobs_finished ON jobs(finished_at,id) WHERE status!='running'");
  exec_sql(db, "CREATE INDEX IF NOT EXISTS jobs_running ON jobs(id) WHERE status='running'");
  exec_sql(db, "CREATE INDEX IF NOT EXISTS messages_history ON messages(dialog_id,id) WHERE role!='context_state'");
  dbdetail::create_dialog_page_state(db);
  if(version<15){version=15;write_schema_version(db,version);}
  if(version<16){exec_sql(db,"CREATE INDEX IF NOT EXISTS steps_task ON steps(task_id,id)");version=16;write_schema_version(db,version);}
  std::filesystem::create_directories(state_root() / "jobs");
}

int Database::schema_version() {
  DbLock lock(impl_->mu);
  return read_schema_version(impl_->db);
}

}  // namespace arli
