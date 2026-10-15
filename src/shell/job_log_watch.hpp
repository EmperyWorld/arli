#pragma once
#include "arli.grpc.pb.h"
#include <QString>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
struct JobLogSnapshot {std::uint64_t generation=0;QString title,body;};
class JobLogWatch {
 public:
  using Publish=std::function<void(JobLogSnapshot)>;
  JobLogWatch(std::shared_ptr<grpc::Channel> channel,Publish publish);
  ~JobLogWatch();
  void start(std::int64_t id,std::int64_t dialog,std::int64_t task,QString title,QString fallback);
  void stop();
  void shutdown();
  std::uint64_t generation();
 private:
  struct Request {std::int64_t id=0,dialog=0,task=0;QString title,fallback;std::uint64_t generation=0;};
  void loop();
  void follow(const Request& request);
  bool current(std::uint64_t generation);
  bool delay(std::uint64_t generation,int ms);
  std::unique_ptr<arli::v1::Agent::Stub> stub_;
  Publish publish_;
  std::mutex mutex_;std::condition_variable wake_;
  Request request_;bool stopping_=false;grpc::ClientContext* active_=nullptr;
  std::thread thread_;
};
