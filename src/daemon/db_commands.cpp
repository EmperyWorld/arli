// Orchestrator-only receipt journal. Never stores command payloads or credentials.
#include "db_impl.hpp"
namespace arli {
using namespace dbdetail;
std::int64_t Database::create_command_receipt(std::int64_t task_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "INSERT INTO command_receipts(task_id) VALUES(?1)", "create receipt");
  st.bind(1, task_id); st.exec("create receipt");
  return sqlite3_last_insert_rowid(impl_->db);
}
void Database::finish_command_receipt(std::int64_t id, const std::string& status) {
  if (status != "accepted" && status != "rejected" && status != "unconfirmed")
    throw std::invalid_argument("invalid receipt status");
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "UPDATE command_receipts SET status=?2 WHERE id=?1 AND status='pending'", "finish receipt");
  st.bind(1, id); st.bind(2, status); st.exec("finish receipt");
}
std::string Database::command_receipt_status(std::int64_t id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT status FROM command_receipts WHERE id=?1", "read receipt");
  st.bind(1, id);
  return st.row() ? col_text(st.get(), 0) : "";
}
}  // namespace arli
