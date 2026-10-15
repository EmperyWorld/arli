#include "db.hpp"
#include "db_impl.hpp"
#include <algorithm>
#include <limits>

namespace arli {
// Keep each query bounded before copying SQLite text into application memory.
std::vector<MessageRow> Database::message_page(std::int64_t dialog, std::int64_t before, int limit, std::int64_t& next) {
  if (dialog <= 0 || before < 0 || limit < 1 || limit > 100) throw std::invalid_argument("invalid message page");
  dbdetail::DbLock lock(impl_->mu);
  dbdetail::Stmt st(impl_->db,
      "SELECT id,role,text,created_at,turn_id FROM messages WHERE dialog_id=?1 "
      "AND role!='context_state' AND id<=?2 ORDER BY id DESC LIMIT ?3", "message page");
  st.bind(1,dialog);st.bind(2,before?before-1:std::numeric_limits<std::int64_t>::max());st.bind(3,limit+1);
  std::vector<MessageRow> rows;std::size_t bytes=0;next=0;
  while(st.row()) {
    std::size_t size=128;
    for(int col=1;col<=3;++col) size+=sqlite3_column_bytes(st.get(),col);
    if(!rows.empty()&&(rows.size()>=static_cast<std::size_t>(limit)||bytes+size>512*1024)) {
      next=rows.back().id;break;
    }
    if(size>3*1024*1024)throw std::length_error("MESSAGE_TOO_LARGE: history item exceeds 3 MiB");
    MessageRow row;row.id=sqlite3_column_int64(st.get(),0);row.dialog_id=dialog;
    row.role=dbdetail::col_text(st.get(),1);row.text=dbdetail::col_text(st.get(),2);
    row.created_at=dbdetail::col_text(st.get(),3);row.turn_id=sqlite3_column_int64(st.get(),4);
    rows.push_back(std::move(row));bytes+=size;
  }
  std::reverse(rows.begin(),rows.end());return rows;
}
} // namespace arli
