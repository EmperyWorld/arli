#pragma once
#include "task_limits.hpp"
#include "arli.grpc.pb.h"
#include <atomic>
#include <condition_variable>
#include <thread>
namespace arli {
class TurnStreamSlot {
  std::atomic<int>& count_;
public:
  explicit TurnStreamSlot(std::atomic<int>& count):count_(count){
    if(count_.fetch_add(1)>=kMaxTurnStreams){count_.fetch_sub(1);throw TaskLimitError("TURN_STREAM_LIMIT: maximum 16 concurrent turn streams");}
  }
  ~TurnStreamSlot(){count_.fetch_sub(1);}
  TurnStreamSlot(const TurnStreamSlot&)=delete;
};
// Bound only a blocked Write, not healthy long tasks or waits for operator input.
class TurnStreamWriter {
  grpc::ServerContext* ctx_;
  grpc::ServerWriter<v1::TurnEvent>* writer_;
  std::mutex mu_;std::condition_variable cv_;bool stopped_=false,writing_=false;
  unsigned generation_=0;std::chrono::steady_clock::time_point deadline_;
  std::thread watchdog_;
public:
  TurnStreamWriter(grpc::ServerContext* ctx,grpc::ServerWriter<v1::TurnEvent>* writer):ctx_(ctx),writer_(writer),watchdog_([this]{
    std::unique_lock lock(mu_);
    while(!stopped_){
      cv_.wait(lock,[&]{return stopped_||writing_;});if(stopped_)return;
      const auto generation=generation_;
      const auto deadline=deadline_;
      if(!cv_.wait_until(lock,deadline,[&]{return stopped_||!writing_||generation_!=generation;})){
        lock.unlock();ctx_->TryCancel();return;
      }
    }
  }){}
  ~TurnStreamWriter(){{std::lock_guard lock(mu_);stopped_=true;}cv_.notify_all();watchdog_.join();}
  bool write(const v1::TurnEvent& event){
    {std::lock_guard lock(mu_);writing_=true;++generation_;deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(10);}cv_.notify_all();
    const bool ok=writer_->Write(event);
    {std::lock_guard lock(mu_);writing_=false;}cv_.notify_all();return ok;
  }
};
}
