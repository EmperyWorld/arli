#include "db.hpp"
#include "db_impl.hpp"

namespace arli {

using namespace dbdetail;

std::vector<MemoryRow> Database::list_memory(int limit) {
  DbLock lock(impl_->mu);
  const int cap = limit > 0 ? limit : 20;
  Stmt st(impl_->db,
          "SELECT id, text, created_at FROM memory ORDER BY id DESC LIMIT ?1",
          "list memory");
  st.bind(1, cap);
  std::vector<MemoryRow> out;
  while (st.row()) {
    MemoryRow row;
    row.id = sqlite3_column_int64(st.get(), 0);
    row.text = col_text(st.get(), 1);
    row.created_at = col_text(st.get(), 2);
    out.push_back(std::move(row));
  }
  return out;
}

std::int64_t Database::next_memory_id() {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT seq FROM sqlite_sequence WHERE name='memory'", "memory sequence");
  return st.row() ? sqlite3_column_int64(st.get(), 0) + 1 : 1;
}

void Database::replace_memory(const std::vector<MemoryRow>& rows) {
  DbLock lock(impl_->mu);
  Stmt begin(impl_->db, "BEGIN IMMEDIATE", "memory transaction"); begin.exec("begin memory");
  try {
    Stmt del(impl_->db, "DELETE FROM memory", "clear memory");
    del.exec("clear memory");
    const std::string now = now_iso();
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
      if (it->text.empty()) {
        continue;
      }
      Stmt st(impl_->db, "INSERT INTO memory(id, text, created_at) VALUES(NULLIF(?3,0),?1,?2)", "insert memory");
      st.bind(1, it->text);
      st.bind(2, it->created_at.empty() ? now : it->created_at);
      st.bind(3, it->id);
      st.exec("insert memory");
    }
    Stmt commit(impl_->db, "COMMIT", "commit memory"); commit.exec("commit memory");
  } catch (...) { sqlite3_exec(impl_->db, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}

}  // namespace arli
