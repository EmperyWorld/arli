#include "job_maintenance.hpp"
#include "jobs.hpp"
#include "job_retention.hpp"
#include "job_orphans.hpp"
#include "log.hpp"
namespace arli {
JobMaintenance::JobMaintenance(std::shared_ptr<Database> db):db_(std::move(db)),thread_([this]{
  std::int64_t cursor=0,retention_cursor=0;int tick=0;
  OrphanJobs orphans;
  std::unique_lock lock(mutex_);
  while(!stop_) {
    lock.unlock();
    try {
      const auto page=db_->running_jobs_after(cursor,64);
      for(auto job:page){cursor=job.id;refresh_job(*db_,std::move(job));}
      if(page.size()<64)cursor=0;
      if(tick==0){retain_jobs(*db_,retention_cursor);orphans.sweep(*db_);db_->retain_history();}
      tick=(tick+1)%30;
    }catch(const std::exception& e){log::warn("jobs",e.what());cursor=0;}
    lock.lock();wake_.wait_for(lock,std::chrono::seconds(1),[this]{return stop_;});
  }
}){}
JobMaintenance::~JobMaintenance(){
  {std::lock_guard lock(mutex_);stop_=true;}wake_.notify_all();thread_.join();
}
}
