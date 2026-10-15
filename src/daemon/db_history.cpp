#include "db.hpp"
#include "db_impl.hpp"

namespace arli {
using namespace dbdetail;

int Database::retain_history(int keep_terminal, int max_age_days, int batch) {
  if (keep_terminal < 1 || max_age_days < 1 || batch < 1 || batch > 512)
    throw std::invalid_argument("invalid history retention limits");
  DbLock lock(impl_->mu);
  Stmt select(impl_->db,
      "WITH recent(id) AS (SELECT id FROM tasks WHERE status IN ('done','failed','cancelled') "
      "ORDER BY finished_at DESC,id DESC LIMIT ?1) "
      "SELECT id FROM tasks WHERE status IN ('done','failed','cancelled') AND "
      "(finished_at<strftime('%Y-%m-%dT%H:%M:%SZ','now',?2) OR id NOT IN (SELECT id FROM recent)) "
      "AND (EXISTS(SELECT 1 FROM events WHERE task_id=tasks.id) OR "
      "EXISTS(SELECT 1 FROM steps WHERE task_id=tasks.id)) ORDER BY id LIMIT ?3",
      "select history retention");
  select.bind(1, keep_terminal); select.bind(2, "-" + std::to_string(max_age_days) + " days"); select.bind(3, batch);
  std::vector<std::int64_t> ids; while (select.row()) ids.push_back(sqlite3_column_int64(select.get(), 0));
  if (ids.empty()) return 0;
  Stmt begin(impl_->db,"BEGIN IMMEDIATE","begin history retention"); begin.exec("begin history retention");
  try {
    for (const auto id : ids) {
      for (const char* sql : {"DELETE FROM events WHERE task_id=?1", "DELETE FROM steps WHERE task_id=?1",
                              "DELETE FROM command_receipts WHERE task_id=?1"}) {
        Stmt drop(impl_->db,sql,"delete retained history"); drop.bind(1,id); drop.exec("delete retained history");
      }
    }
    Stmt commit(impl_->db,"COMMIT","commit history retention"); commit.exec("commit history retention");
    return static_cast<int>(ids.size());
  } catch (...) { sqlite3_exec(impl_->db,"ROLLBACK",nullptr,nullptr,nullptr); throw; }
}
}
