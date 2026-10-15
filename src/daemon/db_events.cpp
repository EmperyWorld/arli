#include "redact.hpp"
#include "db.hpp"
#include "db_impl.hpp"
#include <google/protobuf/util/json_util.h>
namespace arli {
using namespace dbdetail;
std::optional<TaskRow> Database::task_by_request(std::int64_t dialog, const std::string& request) {
  if (request.empty()) return std::nullopt;
  DbLock lock(impl_->mu);
  const auto sql = std::string("SELECT ") + kTaskCols + " FROM tasks WHERE dialog_id=?1 AND request_id=?2";
  Stmt st(impl_->db, sql.c_str(), "request lookup"); st.bind(1, dialog); st.bind(2, request);
  return st.row() ? std::optional<TaskRow>(task_from_row(st.get())) : std::nullopt;
}
bool Database::valid_event_cursor(std::int64_t task, std::int64_t event) {
  if (event == 0) return true;
  if (event < 0) return false;
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT 1 FROM events WHERE task_id=?1 AND id=?2", "validate event cursor");
  st.bind(1, task); st.bind(2, event); return st.row();
}
std::int64_t Database::append_event(std::int64_t task, const v1::TurnEvent& event) {
  DbLock lock(impl_->mu);
  auto safe=event;redact::message(safe);
  std::string payload;
  if (!google::protobuf::util::MessageToJsonString(safe, &payload).ok()) throw std::runtime_error("event serialization failed");
  Stmt st(impl_->db, "INSERT INTO events(task_id,payload) VALUES(?1,?2)", "append event");
  st.bind(1, task); st.bind(2, payload); st.exec("append event");
  return sqlite3_last_insert_rowid(impl_->db);
}
std::vector<v1::TurnEvent> Database::events_after(std::int64_t task, std::int64_t after, int limit) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT id,payload FROM events WHERE task_id=?1 AND id>?2 ORDER BY id LIMIT ?3", "replay events");
  st.bind(1, task); st.bind(2, after); st.bind(3, std::clamp(limit, 1, 64));
  std::vector<v1::TurnEvent> result;
  std::size_t bytes = 0;
  while (st.row()) {
    const auto payload = col_text(st.get(), 1);
    if (!result.empty() && bytes + payload.size() > 4 * 1024 * 1024) break;
    bytes += payload.size();
    v1::TurnEvent event;
    if (!google::protobuf::util::JsonStringToMessage(payload, &event).ok()) throw std::runtime_error("invalid persisted event");
    event.set_event_id(sqlite3_column_int64(st.get(), 0)); event.set_task_id(task);
    redact::message(event);
    result.push_back(std::move(event));
  }
  return result;
}
}
