#include "db.hpp"

#include "db_impl.hpp"

#include <string>

namespace arli {

using namespace dbdetail;

namespace {

constexpr const char* kStepCols =
    "id, dialog_id, turn_id, task_id, step_id, kind, tool, args_json, ok, output, diff, duration_ms, created_at";

StepRow step_from_row(sqlite3_stmt* st) {
  StepRow row;
  row.id = sqlite3_column_int64(st, 0);
  row.dialog_id = sqlite3_column_int64(st, 1);
  row.turn_id = sqlite3_column_int64(st, 2);
  row.task_id = sqlite3_column_int64(st, 3);
  row.step_id = sqlite3_column_int64(st, 4);
  row.kind = col_text(st, 5);
  row.tool = col_text(st, 6);
  row.args_json = col_text(st, 7);
  row.ok = sqlite3_column_int(st, 8) != 0;
  row.output = col_text(st, 9);
  row.diff = col_text(st, 10);
  row.duration_ms = sqlite3_column_int64(st, 11);
  row.created_at = col_text(st, 12);
  return row;
}

}  // namespace

std::int64_t Database::add_step(const StepRow& row) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db,
          "INSERT INTO steps(dialog_id, turn_id, task_id, step_id, kind, tool, args_json, ok, output, diff, "
          "duration_ms, created_at) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)",
          "prep add step");
  st.bind(1, row.dialog_id);
  st.bind(2, row.turn_id);
  st.bind(3, row.task_id);
  st.bind(4, row.step_id);
  st.bind(5, row.kind);
  st.bind(6, row.tool);
  st.bind(7, row.args_json);
  st.bind(8, row.ok ? 1 : 0);
  st.bind(9, row.output.size() > kMaxStepOutput ? row.output.substr(0, kMaxStepOutput) + "\n…[обрезано]"
                                                  : row.output);
  st.bind(10, row.diff.size() > kMaxStepDiff ? row.diff.substr(0, kMaxStepDiff) + "\n…[обрезано]" : row.diff);
  st.bind(11, row.duration_ms);
  st.bind(12, row.created_at.empty() ? now_iso() : row.created_at);
  st.exec("add step");
  return sqlite3_last_insert_rowid(impl_->db);
}

std::vector<StepRow> Database::list_steps_for_task(std::int64_t task_id) {
  DbLock lock(impl_->mu);
  std::vector<StepRow> out;
  const std::string sql = std::string("SELECT ") + kStepCols + " FROM steps WHERE task_id=?1 ORDER BY id ASC";
  Stmt st(impl_->db, sql.c_str(), "prep steps for task");
  st.bind(1, task_id);
  while (st.row()) {
    out.push_back(step_from_row(st.get()));
  }
  return out;
}

std::vector<StepRow> Database::list_steps_for_turn(std::int64_t dialog_id, std::int64_t turn_id) {
  DbLock lock(impl_->mu);
  std::vector<StepRow> out;
  const std::string sql =
      std::string("SELECT ") + kStepCols + " FROM steps WHERE dialog_id=?1 AND turn_id=?2 ORDER BY id ASC";
  Stmt st(impl_->db, sql.c_str(), "prep steps for turn");
  st.bind(1, dialog_id);
  st.bind(2, turn_id);
  while (st.row()) {
    out.push_back(step_from_row(st.get()));
  }
  return out;
}

std::vector<StepRow> Database::list_steps_for_turn_page(std::int64_t dialog_id, std::int64_t turn_id,
                                                        std::int64_t after_id, int limit,
                                                        std::size_t byte_budget, bool& has_more) {
  DbLock lock(impl_->mu);
  std::vector<StepRow> out;
  has_more = false;
  const std::string sql = std::string("SELECT ") + kStepCols +
      " FROM steps WHERE dialog_id=?1 AND turn_id=?2 AND id>?3 ORDER BY id ASC LIMIT ?4";
  Stmt st(impl_->db, sql.c_str(), "prep step page");
  st.bind(1, dialog_id); st.bind(2, turn_id); st.bind(3, after_id); st.bind(4, limit + 1);
  std::size_t bytes = 0;
  while (st.row()) {
    auto row = step_from_row(st.get());
    const auto row_bytes = row.kind.size() + row.tool.size() + row.args_json.size() + row.output.size() +
                           row.diff.size() + row.created_at.size() + 128;
    if (static_cast<int>(out.size()) >= limit || (!out.empty() && bytes + row_bytes > byte_budget)) {
      has_more = true;
      break;
    }
    out.push_back(std::move(row)); bytes += row_bytes;
  }
  return out;
}

std::int64_t Database::last_traced_turn(std::int64_t dialog_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT COALESCE(MAX(turn_id), 0) FROM steps WHERE dialog_id=?1", "prep last traced turn");
  st.bind(1, dialog_id);
  if (!st.row()) {
    return 0;
  }
  return sqlite3_column_int64(st.get(), 0);
}

}  // namespace arli
