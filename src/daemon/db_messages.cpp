#include "redact.hpp"
#include "db.hpp"

#include "db_impl.hpp"

namespace arli {

using namespace dbdetail;

std::vector<MessageRow> Database::list_messages(std::int64_t dialog_id, int limit) {
  DbLock lock(impl_->mu);
  std::vector<MessageRow> out;
  Stmt st(impl_->db,
          "SELECT id, dialog_id, role, text, created_at, turn_id FROM "
          "(SELECT * FROM messages WHERE dialog_id=?1 AND role != 'context_state' ORDER BY id DESC LIMIT ?2) ORDER BY id ASC",
          "prep messages");
  st.bind(1, dialog_id);
  st.bind(2, limit);
  while (st.row()) {
    MessageRow row;
    row.id = sqlite3_column_int64(st.get(), 0);
    row.dialog_id = sqlite3_column_int64(st.get(), 1);
    row.role = col_text(st.get(), 2);
    row.text = col_text(st.get(), 3);
    row.created_at = col_text(st.get(), 4);
    row.turn_id = sqlite3_column_int64(st.get(), 5);
    out.push_back(std::move(row));
  }
  return out;
}

std::string Database::context_state(std::int64_t dialog_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT text FROM messages WHERE dialog_id=?1 AND role='context_state' ORDER BY id DESC LIMIT 1", "read context");
  st.bind(1, dialog_id);
  return st.row() ? col_text(st.get(), 0) : std::string();
}

void Database::save_context_state(std::int64_t dialog_id, const std::string& text) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "UPDATE messages SET text=?2 WHERE dialog_id=?1 AND role='context_state'", "update context");
  st.bind(1, dialog_id);
  st.bind(2, text);
  st.exec("update context");
  if (sqlite3_changes(impl_->db) == 0) add_message(dialog_id, "context_state", text);
}

MessageRow Database::add_message(std::int64_t dialog_id, const std::string& role, const std::string& text,
                                 std::int64_t turn_id) {
  DbLock lock(impl_->mu);
  const std::string ts = now_iso();
  Stmt st(impl_->db, "INSERT INTO messages(dialog_id, role, text, created_at, turn_id) VALUES(?1,?2,?3,?4,?5)",
          "prep add msg");
  st.bind(1, dialog_id);
  st.bind(2, role);
  st.bind(3, redact::text(text));
  st.bind(4, ts);
  st.bind(5, turn_id);
  st.exec("add msg");
  touch_dialog(dialog_id);
  MessageRow row;
  row.id = sqlite3_last_insert_rowid(impl_->db);
  row.dialog_id = dialog_id;
  row.turn_id = turn_id;
  row.role = role;
  row.text = text;
  row.created_at = ts;
  return row;
}

bool Database::rename_dialog(std::int64_t id, const std::string& title) {
  DbLock lock(impl_->mu);
  const std::string t = title.empty() ? "Диалог" : title;
  Stmt st(impl_->db, "UPDATE dialogs SET title=?1, updated_at=?2 WHERE id=?3", "prep rename");
  st.bind(1, t);
  st.bind(2, now_iso());
  st.bind(3, id);
  st.exec("rename dialog");
  return sqlite3_changes(impl_->db) == 1;
}

bool Database::delete_dialog(std::int64_t id) {
  DbLock lock(impl_->mu);
  {
    Stmt st(impl_->db, "DELETE FROM steps WHERE dialog_id=?1", "prep delete steps");
    st.bind(1, id);
    st.exec("delete steps");
  }
  Stmt st(impl_->db, "DELETE FROM dialogs WHERE id=?1", "prep delete dialog");
  st.bind(1, id);
  st.exec("delete dialog");
  return sqlite3_changes(impl_->db) == 1;
}

void Database::touch_dialog(std::int64_t dialog_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "UPDATE dialogs SET updated_at=?1 WHERE id=?2", "prep touch");
  st.bind(1, now_iso());
  st.bind(2, dialog_id);
  st.exec("touch");
}

}  // namespace arli
