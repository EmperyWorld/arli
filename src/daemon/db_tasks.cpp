#include "db.hpp"
#include "task_settings.hpp"
#include "workspace_identity.hpp"
#include "task_limits.hpp"

#include "db_impl.hpp"

#include <string>

namespace arli {

using namespace dbdetail;

namespace {

int fail_where(Database& owner, sqlite3* db, const std::string& reason, const char* where_status) {
  std::vector<std::int64_t> ids;
  {
    const auto sql = std::string("SELECT id FROM tasks WHERE ") + where_status;
    Stmt st(db, sql.c_str(), "find interrupted tasks");
    while (st.row()) ids.push_back(sqlite3_column_int64(st.get(), 0));
  }
  int changed = 0;
  for (auto id : ids) changed += owner.finish_task(id, "failed", reason);
  return changed;
}

}  // namespace

TaskRow Database::enqueue_task(std::int64_t dialog_id, std::int64_t turn_id, const std::string& text, const std::string& request_id, const std::string& revert_changeset,const TaskRow* revert_source) {
  DbLock lock(impl_->mu);
  if(text.size()>kMaxTaskTextBytes)throw TaskLimitError("TASK_TEXT_LIMIT: maximum 262144 bytes");
  const std::string ts = now_iso();
  auto snapshot = nlohmann::json(get_settings());
  if (const auto dialog = get_dialog(dialog_id)) snapshot["dialog_instructions"] = dialog->instructions;
  if (!revert_changeset.empty()) snapshot["revert_changeset_id"] = revert_changeset;
  if(revert_source){snapshot["workdir"]=nlohmann::json::parse(revert_source->config_json).at("workdir");snapshot["revert_source_turn"]=revert_source->turn_id;snapshot["revert_source_task"]=revert_source->id;}
  const auto source_identity=revert_source?nlohmann::json::parse(revert_source->config_json).value("workspace_identity",nlohmann::json()):nlohmann::json();
  const auto identity=source_identity.is_object()?source_identity:capture_workspace(snapshot.at("workdir").get<std::string>());
  snapshot["workspace_identity"]=identity;snapshot["workdir"]=identity.at("path");
  const auto config = snapshot.dump();
  if(config.size()>kMaxTaskSnapshotBytes)throw TaskLimitError("TASK_SNAPSHOT_LIMIT: maximum 262144 bytes");
  Stmt st(impl_->db,
          "INSERT INTO tasks(dialog_id, turn_id, text, status, worker_id, error, created_at, updated_at, finished_at, config_json, request_id) "
          "SELECT ?1,?2,?3,'queued','','',?4,?4,'',?5,?6 "
          "WHERE (SELECT count(*) FROM tasks WHERE status IN ('queued','running'))<?7 "
          "AND (SELECT COALESCE(sum(length(CAST(text AS BLOB))+length(CAST(config_json AS BLOB))),0) "
          "FROM tasks WHERE status IN ('queued','running'))+?8<=?9",
          "prep enqueue");
  st.bind(1, dialog_id);
  st.bind(2, turn_id);
  st.bind(3, text);
  st.bind(4, ts);
  st.bind(5, config);
  st.bind(6, request_id);
  st.bind(7,kMaxActiveTasks);st.bind(8,static_cast<int64_t>(text.size()+config.size()));st.bind(9,kMaxActiveTaskBytes);
  st.exec("enqueue");
  if(sqlite3_changes(impl_->db)!=1)throw TaskLimitError("TASK_QUEUE_CAPACITY: maximum 64 active tasks and 8388608 active bytes; wait or cancel a task");
  TaskRow row;
  row.id = sqlite3_last_insert_rowid(impl_->db);
  row.dialog_id = dialog_id;
  row.turn_id = turn_id;
  row.text = text;
  row.status = "queued";
  row.created_at = ts;
  row.updated_at = ts;
  row.config_json = config;
  return row;
}

std::optional<TaskRow> Database::task_for_turn(std::int64_t dialog,std::int64_t turn){
  DbLock lock(impl_->mu);const auto sql=std::string("SELECT ")+kTaskCols+" FROM tasks WHERE dialog_id=?1 AND turn_id=?2 ORDER BY id DESC LIMIT 1";
  Stmt st(impl_->db,sql.c_str(),"task for turn");st.bind(1,dialog);st.bind(2,turn);if(!st.row())return std::nullopt;return task_from_row(st.get());
}

std::optional<TaskRow> Database::get_task(std::int64_t task_id) {
  DbLock lock(impl_->mu);
  const std::string sql = std::string("SELECT ") + kTaskCols + " FROM tasks WHERE id=?1";
  Stmt st(impl_->db, sql.c_str(), "prep get task");
  st.bind(1, task_id);
  if (!st.row()) {
    return std::nullopt;
  }
  return task_from_row(st.get());
}

bool Database::mark_running(std::int64_t task_id, const std::string& worker_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db,
          "UPDATE tasks SET status='running', worker_id=?1, updated_at=?2 WHERE id=?3 AND status='queued'",
          "prep mark running");
  st.bind(1, worker_id);
  st.bind(2, now_iso());
  st.bind(3, task_id);
  st.exec("mark running");
  return sqlite3_changes(impl_->db) == 1;
}

bool Database::requeue_task(std::int64_t task_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db,
          "UPDATE tasks SET status='queued', worker_id='', updated_at=?1 WHERE id=?2 AND status='running'",
          "prep requeue");
  st.bind(1, now_iso());
  st.bind(2, task_id);
  st.exec("requeue");
  return sqlite3_changes(impl_->db) == 1;
}

void Database::touch_task(std::int64_t task_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "UPDATE tasks SET updated_at=?1 WHERE id=?2 AND status='running'", "prep touch task");
  st.bind(1, now_iso());
  st.bind(2, task_id);
  st.exec("touch task");
}

bool Database::finish_task(std::int64_t task_id, const std::string& status, const std::string& error, v1::TurnEvent* final_event) {
  if (!task_status_terminal(status)) {
    return false;
  }
  DbLock lock(impl_->mu);
  Stmt begin(impl_->db, "BEGIN IMMEDIATE", "finish transaction"); begin.exec("finish transaction");
  try {
    const std::string ts = now_iso();
    Stmt st(impl_->db,
            "UPDATE tasks SET status=?1, error=?2, updated_at=?3, finished_at=?3 "
            "WHERE id=?4 AND status IN ('queued','running')",
            "prep finish task");
    st.bind(1, status);
    st.bind(2, error);
    st.bind(3, ts);
    st.bind(4, task_id);
    st.exec("finish task");
    const bool changed = sqlite3_changes(impl_->db) == 1;
    if (changed) {
      Stmt receipts(impl_->db, "UPDATE command_receipts SET status='unconfirmed' WHERE task_id=?1 AND status='pending'", "finish pending receipts");
      receipts.bind(1, task_id); receipts.exec("finish pending receipts");
      auto task = get_task(task_id);
      v1::TurnEvent event = final_event ? *final_event : v1::TurnEvent{};
      event.set_dialog_id(task->dialog_id); event.set_turn_id(task->turn_id); event.set_task_id(task_id);
      if (event.payload_case() == v1::TurnEvent::PAYLOAD_NOT_SET) {
        if (status == "done") event.mutable_done()->set_outcome(status);
        else event.mutable_error()->set_message(error.empty() ? status : error);
      }
      event.set_event_id(append_event(task_id, event));
      if (final_event) *final_event = event;
    }
    Stmt commit(impl_->db, "COMMIT", "finish commit"); commit.exec("finish commit");
    return changed;
  } catch (...) { sqlite3_exec(impl_->db, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}

int Database::fail_active_tasks(const std::string& reason) {
  DbLock lock(impl_->mu);
  return fail_where(*this, impl_->db, reason, "status IN ('queued','running')");
}

int Database::fail_running_tasks(const std::string& reason) {
  DbLock lock(impl_->mu);
  return fail_where(*this, impl_->db, reason, "status = 'running'");
}

std::optional<TaskRow> Database::next_queued() {
  DbLock lock(impl_->mu);
  const std::string sql =
      std::string("SELECT ") + kTaskCols + " FROM tasks WHERE status='queued' ORDER BY id ASC LIMIT 1";
  Stmt st(impl_->db, sql.c_str(), "prep next queued");
  if (!st.row()) {
    return std::nullopt;
  }
  return task_from_row(st.get());
}

int Database::count_queued() {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT COUNT(*) FROM tasks WHERE status='queued'", "prep count queued");
  if (!st.row()) {
    return 0;
  }
  return sqlite3_column_int(st.get(), 0);
}

int Database::queue_position(std::int64_t task_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db,
          "SELECT COUNT(*) FROM tasks WHERE status='queued' AND id <= ?1 "
          "AND EXISTS (SELECT 1 FROM tasks t WHERE t.id=?1 AND t.status='queued')",
          "prep queue position");
  st.bind(1, task_id);
  if (!st.row()) {
    return 0;
  }
  return sqlite3_column_int(st.get(), 0);
}

std::optional<TaskRow> Database::active_task_for_dialog(std::int64_t dialog_id) {
  DbLock lock(impl_->mu);
  const std::string sql = std::string("SELECT ") + kTaskCols +
                          " FROM tasks WHERE dialog_id=?1 AND status IN ('queued','running') "
                          "ORDER BY id DESC LIMIT 1";
  Stmt st(impl_->db, sql.c_str(), "prep active for dialog");
  st.bind(1, dialog_id);
  if (!st.row()) {
    return std::nullopt;
  }
  return task_from_row(st.get());
}

std::vector<TaskRow> Database::list_tasks(std::int64_t dialog_id, int limit) {
  DbLock lock(impl_->mu);
  std::vector<TaskRow> out;
  const std::string sql = std::string("SELECT ") + kTaskCols +
                          " FROM tasks WHERE (?1 = 0 OR dialog_id = ?1) ORDER BY id DESC LIMIT ?2";
  Stmt st(impl_->db, sql.c_str(), "prep list tasks");
  st.bind(1, dialog_id);
  st.bind(2, limit > 0 ? limit : 50);
  while (st.row()) {
    out.push_back(task_from_row(st.get()));
  }
  return out;
}

std::int64_t Database::next_turn_id(std::int64_t dialog_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT COALESCE(MAX(turn_id), 0) + 1 FROM tasks WHERE dialog_id=?1", "prep next turn");
  st.bind(1, dialog_id);
  if (!st.row()) {
    return 1;
  }
  return sqlite3_column_int64(st.get(), 0);
}

}  // namespace arli
