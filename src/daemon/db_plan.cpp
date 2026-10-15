#include "db.hpp"
#include "db_impl.hpp"

#include <nlohmann/json.hpp>

namespace arli {

using namespace dbdetail;

namespace {

using json = nlohmann::json;

std::vector<std::string> parse_steps(const std::string& raw) {
  std::vector<std::string> out;
  if (raw.empty()) {
    return out;
  }
  try {
    const json j = json::parse(raw);
    if (!j.is_array()) {
      return out;
    }
    for (const auto& item : j) {
      if (item.is_string()) {
        out.push_back(item.get<std::string>());
      }
    }
  } catch (...) {
  }
  return out;
}

std::string dump_steps(const std::vector<std::string>& steps) {
  json arr = json::array();
  for (const auto& s : steps) {
    arr.push_back(s);
  }
  return arr.dump();
}

}  // namespace

std::optional<PlanRow> Database::get_plan(std::int64_t dialog_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT steps_json, current, failed, updated_at FROM plans WHERE dialog_id=?1", "get plan");
  st.bind(1, dialog_id);
  if (!st.row()) {
    return std::nullopt;
  }
  PlanRow row;
  row.steps = parse_steps(col_text(st.get(), 0));
  row.current = sqlite3_column_int(st.get(), 1);
  row.failed = sqlite3_column_int(st.get(), 2) != 0;
  row.updated_at = col_text(st.get(), 3);
  return row;
}

void Database::set_plan(std::int64_t dialog_id, const std::vector<std::string>& steps, int current, bool failed) {
  DbLock lock(impl_->mu);
  const std::string now = dbdetail::now_iso();
  const std::string sj = dump_steps(steps);
  Stmt st(impl_->db,
          "INSERT INTO plans(dialog_id, steps_json, current, failed, updated_at) VALUES(?1,?2,?3,?4,?5) "
          "ON CONFLICT(dialog_id) DO UPDATE SET steps_json=excluded.steps_json, current=excluded.current, "
          "failed=excluded.failed, updated_at=excluded.updated_at",
          "set plan");
  st.bind(1, dialog_id);
  st.bind(2, sj);
  st.bind(3, current);
  st.bind(4, failed ? 1 : 0);
  st.bind(5, now);
  st.exec("set plan");
}

void Database::clear_plan(std::int64_t dialog_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "DELETE FROM plans WHERE dialog_id=?1", "clear plan");
  st.bind(1, dialog_id);
  st.exec("clear plan");
}

}  // namespace arli
