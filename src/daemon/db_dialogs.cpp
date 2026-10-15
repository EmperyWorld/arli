#include "db.hpp"
#include "db_impl.hpp"

namespace arli {

using namespace dbdetail;

std::vector<DialogRow> Database::list_dialogs() {
  DbLock lock(impl_->mu);
  std::vector<DialogRow> out;
  Stmt st(impl_->db, "SELECT id, title, updated_at, instructions FROM dialogs ORDER BY updated_at DESC, id DESC LIMIT 1001", "prep dialogs");
  std::size_t bytes=0;
  while (st.row()) {
    for(int col=1;col<=3;++col)bytes+=sqlite3_column_bytes(st.get(),col);
    bytes+=128;
    if(out.size()>=1000||bytes>3*1024*1024)throw std::length_error("DIALOG_LIST_TOO_LARGE: use ListDialogPage");
    DialogRow row;
    row.id = sqlite3_column_int64(st.get(), 0);
    row.title = col_text(st.get(), 1);
    row.updated_at = col_text(st.get(), 2);
    row.instructions = col_text(st.get(), 3);
    out.push_back(std::move(row));
  }
  return out;
}

bool Database::set_dialog_instructions(std::int64_t id, const std::string& text) {
  if (text.size() > 16384 || text.find('\0') != std::string::npos) return false;
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "UPDATE dialogs SET instructions=?2, updated_at=?3 WHERE id=?1", "set dialog instructions");
  st.bind(1, id); st.bind(2, text); st.bind(3, now_iso()); st.exec("set dialog instructions");
  return sqlite3_changes(impl_->db) == 1;
}

DialogRow Database::create_dialog(const std::string& title) {
  DbLock lock(impl_->mu);
  const std::string ts = now_iso();
  const std::string t = title.empty() ? "Диалог" : title;
  Stmt st(impl_->db, "INSERT INTO dialogs(title, updated_at) VALUES(?1, ?2)", "prep create");
  st.bind(1, t);
  st.bind(2, ts);
  st.exec("create dialog");
  DialogRow row;
  row.id = sqlite3_last_insert_rowid(impl_->db);
  row.title = t;
  row.updated_at = ts;
  return row;
}

std::optional<DialogRow> Database::get_dialog(std::int64_t id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT id, title, updated_at, instructions FROM dialogs WHERE id=?1", "prep get");
  st.bind(1, id);
  if (!st.row()) {
    return std::nullopt;
  }
  DialogRow row;
  row.id = sqlite3_column_int64(st.get(), 0);
  row.title = col_text(st.get(), 1);
  row.updated_at = col_text(st.get(), 2);
    row.instructions = col_text(st.get(), 3);
  return row;
}

}  // namespace arli
