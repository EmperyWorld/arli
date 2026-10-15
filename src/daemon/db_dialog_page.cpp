#include "db.hpp"
#include "db_impl.hpp"
#include <nlohmann/json.hpp>

namespace arli {
namespace dbdetail {
void create_dialog_page_state(sqlite3* db) {
  const char* sql=R"sql(
CREATE TABLE IF NOT EXISTS dialog_list_state(id INTEGER PRIMARY KEY CHECK(id=1),revision INTEGER NOT NULL);
INSERT OR IGNORE INTO dialog_list_state VALUES(1,1);
CREATE INDEX IF NOT EXISTS dialogs_recency ON dialogs(updated_at DESC,id DESC);
CREATE TRIGGER IF NOT EXISTS dialogs_page_insert AFTER INSERT ON dialogs BEGIN
  UPDATE dialog_list_state SET revision=revision+1 WHERE id=1;
END;
CREATE TRIGGER IF NOT EXISTS dialogs_page_update AFTER UPDATE ON dialogs BEGIN
  UPDATE dialog_list_state SET revision=revision+1 WHERE id=1;
END;
CREATE TRIGGER IF NOT EXISTS dialogs_page_delete AFTER DELETE ON dialogs BEGIN
  UPDATE dialog_list_state SET revision=revision+1 WHERE id=1;
END;
)sql";
  check(sqlite3_exec(db,sql,nullptr,nullptr,nullptr),db,"dialog page schema");
}
}
std::vector<DialogRow> Database::dialog_page(const std::string& token,int limit,std::string& next) {
  if(limit<1||limit>100||token.size()>512)throw std::invalid_argument("invalid dialog page");
  std::int64_t revision=0,before=0;std::string at;
  if(!token.empty())try {
    const auto value=nlohmann::json::parse(token);
    revision=value.at("revision").get<std::int64_t>();before=value.at("id").get<std::int64_t>();at=value.at("at").get<std::string>();
    if(revision<=0||before<=0||at.empty()||at.size()>64||at.find('\0')!=std::string::npos)throw std::invalid_argument("cursor");
  }catch(...){throw std::invalid_argument("invalid dialog page token");}
  dbdetail::DbLock lock(impl_->mu);
  // One SQLite statement observes revision and rows in the same read snapshot,
  // including updates made by a different process. Limit title bytes in SQL too.
  const std::string sql="SELECT s.revision,d.id,substr(CAST(d.title AS BLOB),1,262145),d.updated_at FROM dialog_list_state s "
    "LEFT JOIN (SELECT id,updated_at FROM dialogs "+std::string(before?"WHERE (updated_at,id)<(?1,?2) ":"")+
    "ORDER BY updated_at DESC,id DESC LIMIT ?3) p ON 1 LEFT JOIN dialogs d ON d.id=p.id "
    "WHERE s.id=1 ORDER BY p.updated_at DESC,p.id DESC";
  dbdetail::Stmt st(impl_->db,sql.c_str(),"dialog page");st.bind(1,at);st.bind(2,before);st.bind(3,limit+1);
  std::vector<DialogRow> rows;std::size_t bytes=0;next.clear();
  while(st.row()) {
    const auto current=sqlite3_column_int64(st.get(),0);
    if(revision&&revision!=current)throw std::domain_error("DIALOG_LIST_CHANGED: restart listing");
    revision=current;if(sqlite3_column_type(st.get(),1)==SQLITE_NULL)break;
    const auto size=static_cast<std::size_t>(sqlite3_column_bytes(st.get(),2))+128;
    if(!rows.empty()&&(rows.size()>=static_cast<std::size_t>(limit)||bytes+size>512*1024)) {
      const auto& last=rows.back();next=nlohmann::json{{"revision",revision},{"id",last.id},{"at",last.updated_at}}.dump();break;
    }
    if(size>256*1024+128)throw std::length_error("DIALOG_TITLE_TOO_LARGE");
    DialogRow row;row.id=sqlite3_column_int64(st.get(),1);row.title=dbdetail::col_text(st.get(),2);row.updated_at=dbdetail::col_text(st.get(),3);
    if(row.updated_at.size()>64)throw std::length_error("DIALOG_TIMESTAMP_TOO_LARGE");
    rows.push_back(std::move(row));bytes+=size;
  }
  return rows;
}
} // namespace arli
