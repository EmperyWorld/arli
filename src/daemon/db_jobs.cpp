#include "db.hpp"
#include "job_policy.hpp"
#include "job_retention.hpp"

#include "db_impl.hpp"

#include <stdexcept>
#include <string>

namespace arli {
namespace dbdetail {

void create_jobs_table(sqlite3* db) {
  char* err = nullptr;
  const char* sql =
      "CREATE TABLE IF NOT EXISTS jobs ("
      " id INTEGER PRIMARY KEY AUTOINCREMENT,"
      " pid INTEGER NOT NULL,"
      " command TEXT NOT NULL,"
      " workdir TEXT NOT NULL,"
      " log_path TEXT NOT NULL,"
      " status TEXT NOT NULL,"
      " started_at TEXT NOT NULL"
      ");";
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "create jobs";
    sqlite3_free(err);
    throw std::runtime_error(msg);
  }
}

}  // namespace dbdetail

namespace {

JobRow job_from(sqlite3_stmt* st) {
  JobRow row;
  row.id = sqlite3_column_int64(st, 0);
  row.pid = sqlite3_column_int(st, 1);
  row.command = dbdetail::col_text(st, 2);
  row.workdir = dbdetail::col_text(st, 3);
  row.log_path = dbdetail::col_text(st, 4);
  row.status = dbdetail::col_text(st, 5);
  row.started_at = dbdetail::col_text(st, 6);
  row.process_identity = dbdetail::col_text(st, 7);
  row.exit_code = sqlite3_column_int(st, 8);
  row.dialog_id = sqlite3_column_int64(st, 9);
  row.task_id = sqlite3_column_int64(st, 10);
  row.turn_id = sqlite3_column_int64(st, 11);
  row.finished_at = dbdetail::col_text(st,12);
  return row;
}

}  // namespace

std::int64_t Database::add_job(int pid, const std::string& command, const std::string& workdir,
                               const std::string& log_path, const std::string& process_identity,
                               std::int64_t dialog_id, std::int64_t task_id, std::int64_t turn_id) {
  dbdetail::DbLock lock(impl_->mu);
  if(dialog_id<0||task_id<0||turn_id<0||(dialog_id==0)!=(turn_id==0)||(task_id!=0&&dialog_id==0))
    throw std::runtime_error("invalid job owner");
  if(dialog_id&&!get_dialog(dialog_id))throw std::runtime_error("job dialog not found");
  if(task_id){const auto task=get_task(task_id);if(!task||task->dialog_id!=dialog_id||task->turn_id!=turn_id||task->status!="running")
    throw std::runtime_error("job task ownership mismatch or task no longer running");}
  const std::string ts = dbdetail::now_iso();
  dbdetail::Stmt st(impl_->db,
                    "INSERT INTO jobs(pid, command, workdir, log_path, status, started_at, process_identity, dialog_id, task_id, turn_id) "
                    "SELECT ?1,?2,?3,?4,'running',?5,?6,?7,?8,?9 "
                    "WHERE (?7=0 OR EXISTS(SELECT 1 FROM dialogs WHERE id=?7)) "
                    "AND (?8=0 OR EXISTS(SELECT 1 FROM tasks WHERE id=?8 AND dialog_id=?7 AND turn_id=?9 AND status='running')) "
                    "AND (SELECT count(*) FROM jobs WHERE status='running')<?10 "
                    "AND (?7=0 OR (SELECT count(*) FROM jobs WHERE status='running' AND dialog_id=?7)<?11) "
                    "AND (?8=0 OR (SELECT count(*) FROM jobs WHERE status='running' AND task_id=?8)<?12)",
                    "prep add job");
  st.bind(1, pid);
  st.bind(2, command);
  st.bind(3, workdir);
  st.bind(4, log_path);
  st.bind(5, ts);
  st.bind(6, process_identity);
  st.bind(7, dialog_id);st.bind(8, task_id);st.bind(9, turn_id);
  st.bind(10,kMaxRunningJobs);st.bind(11,kMaxDialogJobs);st.bind(12,kMaxTaskJobs);
  st.exec("add job");
  if(sqlite3_changes(impl_->db)!=1)throw std::runtime_error("JOB_ADMISSION_REJECTED: owner inactive or background quota reached");
  return sqlite3_last_insert_rowid(impl_->db);
}

std::vector<JobRow> Database::list_jobs(int limit, std::int64_t dialog_id, std::int64_t task_id) {
  dbdetail::DbLock lock(impl_->mu);
  if (limit <= 0) {
    limit = 30;
  }
  std::vector<JobRow> out;
  dbdetail::Stmt st(impl_->db,
                    "SELECT id, pid, command, workdir, log_path, status, started_at, process_identity, exit_code, dialog_id, task_id, turn_id, finished_at "
                    "FROM jobs WHERE (?2=0 OR dialog_id=?2) AND (?3=0 OR task_id=?3) ORDER BY (status='running') DESC, id DESC LIMIT ?1",
                    "prep list jobs");
  st.bind(1, std::min(limit,1000));
  st.bind(2,dialog_id);st.bind(3,task_id);
  while (st.row()) {
    out.push_back(job_from(st.get()));
  }
  return out;
}

bool Database::job_capacity_available(std::int64_t dialog_id,std::int64_t task_id) {
  dbdetail::DbLock lock(impl_->mu);
  dbdetail::Stmt st(impl_->db,"SELECT count(*),coalesce(sum(dialog_id=?1),0),coalesce(sum(task_id=?2),0) FROM jobs WHERE status='running'","job admission preflight");
  st.bind(1,dialog_id);st.bind(2,task_id);if(!st.row())return false;
  return sqlite3_column_int64(st.get(),0)<kMaxRunningJobs&&(!dialog_id||sqlite3_column_int64(st.get(),1)<kMaxDialogJobs)&&
    (!task_id||sqlite3_column_int64(st.get(),2)<kMaxTaskJobs);
}
std::vector<JobRow> Database::running_jobs_after(std::int64_t after,int limit) {
  dbdetail::DbLock lock(impl_->mu);
  dbdetail::Stmt st(impl_->db,"SELECT id,pid,command,workdir,log_path,status,started_at,process_identity,exit_code,dialog_id,task_id,turn_id,finished_at "
    "FROM jobs WHERE status='running' AND id>?1 ORDER BY id LIMIT ?2","running job page");
  st.bind(1,after);st.bind(2,std::clamp(limit,1,64));std::vector<JobRow> out;
  while(st.row())out.push_back(job_from(st.get()));return out;
}

std::optional<JobRow> Database::get_job(std::int64_t id) {
  dbdetail::DbLock lock(impl_->mu);
  dbdetail::Stmt st(impl_->db,
                    "SELECT id, pid, command, workdir, log_path, status, started_at, process_identity, exit_code, dialog_id, task_id, turn_id, finished_at "
                    "FROM jobs WHERE id=?1",
                    "prep get job");
  st.bind(1, id);
  if (!st.row()) {
    return std::nullopt;
  }
  return job_from(st.get());
}

void Database::finish_job(std::int64_t id, const std::string& status, int exit_code) {
  if(status!="exited"&&status!="stopped"&&status!="lost")throw std::runtime_error("invalid terminal job status");
  dbdetail::DbLock lock(impl_->mu);
  dbdetail::Stmt st(impl_->db, "UPDATE jobs SET status=?1,exit_code=?2,finished_at=?4 WHERE id=?3 AND status='running'", "finish job");
  st.bind(1, status); st.bind(2, exit_code); st.bind(3, id);st.bind(4,dbdetail::now_iso()); st.exec("finish job");
}

void Database::set_job_status(std::int64_t id, const std::string& status) {
  if(status!="lost")throw std::runtime_error("invalid background status transition");
  finish_job(id,status,-1);
}
std::vector<JobRow> Database::expired_jobs_after(std::int64_t after,int limit) {
  dbdetail::DbLock lock(impl_->mu);
  dbdetail::Stmt st(impl_->db,"SELECT id,pid,command,workdir,log_path,status,started_at,process_identity,exit_code,dialog_id,task_id,turn_id,finished_at "
    "FROM jobs WHERE status!='running' AND id>?1 AND (finished_at<strftime('%Y-%m-%dT%H:%M:%SZ','now',?2) OR "
    "id NOT IN (SELECT id FROM jobs WHERE status!='running' ORDER BY finished_at DESC,id DESC LIMIT ?3)) ORDER BY id LIMIT ?4","job retention page");
  st.bind(1,after);st.bind(2,"-"+std::to_string(kJobRetentionDays)+" days");st.bind(3,kRetainedJobs);st.bind(4,std::clamp(limit,1,32));
  std::vector<JobRow> out;while(st.row())out.push_back(job_from(st.get()));return out;
}
bool Database::forget_job(const JobRow& job) {
  dbdetail::DbLock lock(impl_->mu);
  dbdetail::Stmt st(impl_->db,"DELETE FROM jobs WHERE id=?1 AND status!='running' AND process_identity=?2 AND log_path=?3 AND finished_at=?4","forget expired job");
  st.bind(1,job.id);st.bind(2,job.process_identity);st.bind(3,job.log_path);st.bind(4,job.finished_at);st.exec("forget expired job");return sqlite3_changes(impl_->db)==1;
}

bool Database::job_log_registered(const std::string& path) {
  dbdetail::DbLock lock(impl_->mu);
  dbdetail::Stmt st(impl_->db,"SELECT 1 FROM jobs WHERE log_path=?1 OR substr(log_path,-21)=?2 LIMIT 1","registered job path");
  st.bind(1,path);st.bind(2,path.substr(path.size()>21?path.size()-21:0));return st.row();
}

}  // namespace arli
