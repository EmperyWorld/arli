#include "db.hpp"
#include "db_impl.hpp"

#include <nlohmann/json.hpp>

namespace arli {

using namespace dbdetail;

namespace {

using json = nlohmann::json;

std::vector<std::string> parse_path_array(const std::string& raw) {
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

std::string dump_paths(const std::vector<std::string>& paths) {
  json arr = json::array();
  for (const auto& p : paths) {
    arr.push_back(p);
  }
  return arr.dump();
}

}  // namespace

std::optional<FilePassRow> Database::get_file_pass(std::int64_t dialog_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "SELECT paths_json, done_json, updated_at, reviewed_json FROM file_pass WHERE dialog_id=?1", "get file_pass");
  st.bind(1, dialog_id);
  if (!st.row()) {
    return std::nullopt;
  }
  FilePassRow row;
  row.paths = parse_path_array(col_text(st.get(), 0));
  row.done = parse_path_array(col_text(st.get(), 1));
  row.updated_at = col_text(st.get(), 2);
  row.reviewed = parse_path_array(col_text(st.get(), 3));
  return row;
}

void Database::set_file_pass(std::int64_t dialog_id, const std::vector<std::string>& paths,
                             const std::vector<std::string>& done, const std::vector<std::string>& reviewed) {
  DbLock lock(impl_->mu);
  const std::string now = dbdetail::now_iso();
  const std::string pj = dump_paths(paths);
  const std::string dj = dump_paths(done);
  Stmt st(impl_->db,
          "INSERT INTO file_pass(dialog_id, paths_json, done_json, updated_at, reviewed_json) VALUES(?1,?2,?3,?4,?5) "
          "ON CONFLICT(dialog_id) DO UPDATE SET paths_json=excluded.paths_json, done_json=excluded.done_json, "
          "updated_at=excluded.updated_at, reviewed_json=excluded.reviewed_json",
          "set file_pass");
  st.bind(1, dialog_id);
  st.bind(2, pj);
  st.bind(3, dj);
  st.bind(4, now);
  st.bind(5, dump_paths(reviewed));
  st.exec("set file_pass");
}

void Database::clear_file_pass(std::int64_t dialog_id) {
  DbLock lock(impl_->mu);
  Stmt st(impl_->db, "DELETE FROM file_pass WHERE dialog_id=?1", "clear file_pass");
  st.bind(1, dialog_id);
  st.exec("clear file_pass");
}

}  // namespace arli
