#include "job_log_watch.hpp"
#include "auth.hpp"
#include "secrets.hpp"
#include <algorithm>
JobLogWatch::JobLogWatch(std::shared_ptr<grpc::Channel> channel,Publish publish)
    :stub_(arli::v1::Agent::NewStub(channel)),publish_(std::move(publish)),thread_([this]{loop();}){}
JobLogWatch::~JobLogWatch(){shutdown();}
void JobLogWatch::start(std::int64_t id,std::int64_t dialog,std::int64_t task,QString title,QString fallback){
  {std::lock_guard lock(mutex_);request_={id,dialog,task,std::move(title),std::move(fallback),request_.generation+1};if(active_)active_->TryCancel();}wake_.notify_all();
}
void JobLogWatch::stop(){start(0,0,0,{},{});}
void JobLogWatch::shutdown(){
  {std::lock_guard lock(mutex_);stopping_=true;if(active_)active_->TryCancel();}wake_.notify_all();
  if(thread_.joinable())thread_.join();
}
std::uint64_t JobLogWatch::generation(){std::lock_guard lock(mutex_);return request_.generation;}
bool JobLogWatch::current(std::uint64_t generation){std::lock_guard lock(mutex_);return !stopping_&&request_.generation==generation;}
bool JobLogWatch::delay(std::uint64_t generation,int ms){
  std::unique_lock lock(mutex_);return !wake_.wait_for(lock,std::chrono::milliseconds(ms),[&]{return stopping_||request_.generation!=generation;});
}
void JobLogWatch::loop(){
  std::unique_lock lock(mutex_);std::uint64_t handled=0;
  while(!stopping_){
    wake_.wait(lock,[&]{return stopping_||request_.generation!=handled;});if(stopping_)break;
    const auto request=request_;handled=request.generation;lock.unlock();if(request.id)follow(request);lock.lock();
  }
}
void JobLogWatch::follow(const Request& request){
  QString body;std::string position;bool gap=false,trimmed=false;int retry=200;
  auto publish=[&](const QString& status){
    QString notice;if(gap)notice+=QStringLiteral("[Часть вывода недоступна после переподключения или ротации.]\n");
    if(trimmed)notice+=QStringLiteral("[Показаны последние 65 536 символов.]\n");
    publish_({request.generation,request.title+QStringLiteral(" · ")+status,notice+body});
  };
  while(current(request.generation)){
    grpc::ClientContext ctx;arli::auth::bearer(ctx,arli::secrets::ui_token());ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(20));
    {std::lock_guard lock(mutex_);if(stopping_||request_.generation!=request.generation)return;active_=&ctx;}
    arli::v1::WatchJobLogRequest query;query.set_id(request.id);query.set_expected_dialog_id(request.dialog);query.set_expected_task_id(request.task);query.set_after_position(position);
    auto stream=stub_->WatchJobLog(&ctx,query);arli::v1::JobLogChunk chunk;bool terminal=false;
    while(stream->Read(&chunk)){
      if(!current(request.generation)){ctx.TryCancel();break;}
      gap|=chunk.gap();body+=QString::fromStdString(chunk.text());
      if(body.size()>65536){body=body.right(65536);if(!body.isEmpty()&&body.front().isLowSurrogate())body.remove(0,1);trimmed=true;}
      position=chunk.next_position();terminal=chunk.terminal();retry=200;
      const auto status=terminal?(chunk.exit_code_known()?QStringLiteral("код выхода ")+QString::number(chunk.exit_code()):QStringLiteral("связь с процессом потеряна")):QStringLiteral("выполняется");
      publish(status);
    }
    const auto status=stream->Finish();
    {std::lock_guard lock(mutex_);if(active_==&ctx)active_=nullptr;}
    if(!current(request.generation)||terminal)return;
    if(status.error_code()==grpc::StatusCode::INVALID_ARGUMENT&&!position.empty()){
      position.clear();body.clear();gap=true;trimmed=false;
    }else if(status.error_code()==grpc::StatusCode::UNIMPLEMENTED||status.error_code()==grpc::StatusCode::FAILED_PRECONDITION){
      if(body.isEmpty())body=request.fallback;publish(QStringLiteral("поток недоступен; сохранённый вывод"));return;
    }else if(status.error_code()==grpc::StatusCode::NOT_FOUND||status.error_code()==grpc::StatusCode::UNAUTHENTICATED||
             status.error_code()==grpc::StatusCode::PERMISSION_DENIED||status.error_code()==grpc::StatusCode::INVALID_ARGUMENT){
      publish(QStringLiteral("просмотр недоступен"));return;
    }
    publish(QStringLiteral("переподключение…"));if(!delay(request.generation,retry))return;retry=std::min(retry*2,2000);
  }
}
