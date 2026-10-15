#include "facade.hpp"
#include "auth.hpp"
#include "arli.grpc.pb.h"
#include <QGuiApplication>
#include <QTest>
#include <QElapsedTimer>
#include <atomic>
#include <iostream>
class Service final:public arli::v1::Agent::Service {
 public:
  std::atomic<int> calls{0},cancelled{0},stop_calls{0},resets{0};std::atomic<bool> resumed{false},second{false};
  grpc::Status Health(grpc::ServerContext*,const arli::v1::HealthRequest*,arli::v1::HealthReply* out)override{out->set_serving(true);return grpc::Status::OK;}
  grpc::Status ListJobs(grpc::ServerContext*,const arli::v1::ListJobsRequest*,arli::v1::ListJobsReply* out)override{
    for(int id=1;id<=5;++id){auto* job=out->add_jobs();job->set_id(id);job->set_command("job "+std::to_string(id));job->set_status("running");job->set_log_tail("legacy tail");}return grpc::Status::OK;
  }
  grpc::Status StopJob(grpc::ServerContext*,const arli::v1::StopJobRequest*,arli::v1::StopJobReply* out)override{++stop_calls;out->set_ok(true);return grpc::Status::OK;}
  grpc::Status WatchJobLog(grpc::ServerContext* ctx,const arli::v1::WatchJobLogRequest* req,grpc::ServerWriter<arli::v1::JobLogChunk>* writer)override{
    ++calls;arli::v1::JobLogChunk chunk;chunk.set_id(req->id());
    if(req->id()==5){
      const auto phase=resets.fetch_add(1);
      if(phase==0){chunk.set_text("OLD");chunk.set_next_position("expired");writer->Write(chunk);return grpc::Status::CANCELLED;}
      if(phase==1)return {grpc::StatusCode::INVALID_ARGUMENT,"rotated cursor key"};
      resumed=req->after_position().empty();chunk.set_text("NEW");chunk.set_terminal(true);writer->Write(chunk);return grpc::Status::OK;
    }
    if(req->id()==3)return {grpc::StatusCode::UNIMPLEMENTED,"legacy"};
    if(req->id()==4){for(int n=0;n<80;++n){chunk.set_text(std::string(4096,'x')+"✅");chunk.set_next_position(std::to_string(n));if(!writer->Write(chunk))break;}chunk.set_text("END");chunk.set_terminal(true);writer->Write(chunk);return grpc::Status::OK;}
    if(req->id()==1&&req->after_position().empty()){chunk.set_text("first ✅");chunk.set_next_position("p1");writer->Write(chunk);return grpc::Status::CANCELLED;}
    if(req->id()==1){resumed=req->after_position()=="p1";chunk.set_text(" second");chunk.set_next_position("p2");writer->Write(chunk);}
    else {second=true;chunk.set_text("other log");chunk.set_next_position("q1");chunk.set_gap(true);writer->Write(chunk);}
    while(!ctx->IsCancelled())std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ++cancelled;chunk.set_text("STALE");writer->Write(chunk);return grpc::Status::CANCELLED;
  }
};
int main(int argc,char** argv){
  QGuiApplication app(argc,argv);Service service;grpc::ServerBuilder builder;int port=0;
  builder.AddListeningPort("127.0.0.1:0",arli::auth::server_credentials("ui-fixture","internal-fixture"),&port);builder.RegisterService(&service);auto server=builder.BuildAndStart();if(!server)return 2;
  qputenv("ARLI_LISTEN",QByteArray("127.0.0.1:")+QByteArray::number(port));qputenv("ARLI_UI_TOKEN","ui-fixture");
  int failures=0;auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<why<<'\n';}};
  auto wait=[&](auto predicate,int ms=3000){QElapsedTimer time;time.start();while(!predicate()&&time.elapsed()<ms)QTest::qWait(10);return predicate();};
  auto facade=std::make_unique<AgentFacade>();check(wait([&]{return facade->jobs().size()==5;}),"jobs loaded through real RPC");
  facade->openJobLog(0);check(wait([&]{return service.resumed&&facade->inspectorBody()==QStringLiteral("first ✅ second");}),"Qt stream reconnects with applied position without duplicates");
  QElapsedTimer switch_time;switch_time.start();facade->openJobLog(1);check(switch_time.elapsed()<100,"switch does not wait for old network call");
  check(wait([&]{return service.second&&facade->inspectorBody().contains("other log");}),"new job selected");
  QTest::qWait(100);check(!facade->inspectorBody().contains("first")&&!facade->inspectorBody().contains("STALE")&&facade->inspectorBody().contains(QStringLiteral("недоступна")),"old generation rejected, gap visible");
  facade->stopJob(2);check(wait([&]{return service.stop_calls>0;}),"control RPC remains responsive during stream");
  const int before=service.cancelled;switch_time.restart();facade->stopJobLog();check(switch_time.elapsed()<100&&!facade->jobLogSelected(),"detach is immediate");
  check(wait([&]{return service.cancelled>before;}),"detach cancels server context without stopping job");
  facade->openJobLog(2);check(wait([&]{return facade->inspectorBody()=="legacy tail"&&facade->inspectorTitle().contains(QStringLiteral("поток недоступен"));}),"legacy server fallback explicitly marked");
  facade->openJobLog(3);QTest::qSleep(150);check(wait([&]{return facade->inspectorBody().endsWith("END");}),"coalesced updates retain final output");
  check(facade->inspectorBody().size()<66000&&facade->inspectorBody().contains(QStringLiteral("последние"))&&!facade->inspectorBody().contains(QChar::ReplacementCharacter),"UI tail bounded without broken surrogate pair");
  facade->openJobLog(4);check(wait([&]{return facade->inspectorBody().contains("NEW");}),"invalidated cursor restarts observation");
  check(service.resumed&&!facade->inspectorBody().contains("OLD")&&facade->inspectorBody().contains(QStringLiteral("недоступна")),"reset replaces stale content with visible discontinuity");
  facade->openJobLog(1);check(wait([&]{return facade->inspectorBody().contains("other log");}),"watch active before destruction");
  switch_time.restart();facade.reset();check(switch_time.elapsed()<1500,"facade shutdown cancels active log stream");
  server->Shutdown();std::cout<<"JOB_LOG_UI_"<<(failures?"FAIL":"OK")<<'\n';return failures?1:0;
}
