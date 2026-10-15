#include "facade.hpp"
#include "auth.hpp"
#include "arli.grpc.pb.h"
#include <QGuiApplication>
#include <QTest>
#include <QElapsedTimer>
#include <QAbstractItemModelTester>
#include <atomic>
#include <iostream>
class Service final:public arli::v1::Agent::Service {
 public:
  std::atomic<int> calls{0},fallbacks{0};std::atomic<bool> changed{false},legacy{false},fail{false},delay{false},entered{false},release{false};
  grpc::Status Health(grpc::ServerContext*,const arli::v1::HealthRequest*,arli::v1::HealthReply* r)override{r->set_serving(true);return grpc::Status::OK;}
  grpc::Status ListDialogPage(grpc::ServerContext* ctx,const arli::v1::ListDialogPageRequest* q,arli::v1::ListDialogPageReply* r)override{
    ++calls;if(legacy)return {grpc::StatusCode::UNIMPLEMENTED,"old daemon"};if(fail)return {grpc::StatusCode::UNAVAILABLE,"injected failure"};
    if(!q->page_token().empty()&&changed)return {grpc::StatusCode::ABORTED,"changed"};
    if(delay&&!q->page_token().empty()){entered=true;while(!release&&!ctx->IsCancelled())std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    const int page=q->page_token().empty()?0:std::stoi(q->page_token());
    for(int n=150-page*50;n>100-page*50;--n){auto* d=r->add_dialogs();d->set_id(n);d->set_title(n==75?"ПРОВЕРКА проекта":"Other "+std::to_string(n));d->set_updated_at("2026-10-11");}
    if(page<2)r->set_next_page_token(std::to_string(page+1));return grpc::Status::OK;
  }
  grpc::Status ListDialogs(grpc::ServerContext*,const arli::v1::ListDialogsRequest*,arli::v1::ListDialogsReply* r)override{++fallbacks;auto* d=r->add_dialogs();d->set_id(1);d->set_title("Legacy");return grpc::Status::OK;}
  grpc::Status OpenDialog(grpc::ServerContext*,const arli::v1::OpenDialogRequest* q,arli::v1::OpenDialogReply* r)override{r->mutable_dialog()->set_id(q->dialog_id());return grpc::Status::OK;}
};
int main(int argc,char** argv){
  QGuiApplication app(argc,argv);Service service;grpc::ServerBuilder b;int port=0;b.AddListeningPort("127.0.0.1:0",arli::auth::server_credentials("dialog-ui","internal-ui"),&port);b.RegisterService(&service);auto server=b.BuildAndStart();if(!server)return 2;
  qputenv("ARLI_LISTEN",QByteArray("127.0.0.1:")+QByteArray::number(port));qputenv("ARLI_UI_TOKEN","dialog-ui");
  int failures=0;auto check=[&](bool yes,const char* why){if(!yes){++failures;std::cerr<<why<<'\n';}};
  auto wait=[&](auto fn){QElapsedTimer t;t.start();while(!fn()&&t.elapsed()<4000)QTest::qWait(10);return fn();};
  {AgentFacade facade;auto* model=facade.dialogs();QAbstractItemModelTester tester(model,QAbstractItemModelTester::FailureReportingMode::Fatal);
    check(wait([&]{return facade.dialogId()==150&&!model->loading();})&&model->rowCount()==50&&model->hasMore(),"first bounded page opens most recent dialog");
    model->loadMore();model->loadMore();check(wait([&]{return !model->loading();})&&model->rowCount()==100&&service.calls==2,"append with single-flight");
    service.changed=true;model->loadMore();check(wait([&]{return !model->loading();})&&model->refreshRequired()&&model->rowCount()==100,"changed list explicitly requests refresh without losing displayed rows");
    service.changed=false;model->loadMore();check(wait([&]{return !model->loading();})&&model->rowCount()==50,"refresh starts from first page");
    facade.setDialogFilter(QStringLiteral("проверка"));check(wait([&]{return !model->loading();})&&model->rowCount()==1&&model->idAt(0)==75&&!model->hasMore(),"Unicode insensitive search traverses unloaded pages");
    facade.setDialogFilter("");check(wait([&]{return !model->loading();})&&model->rowCount()==50,"clear search restores first page");
    service.delay=true;model->loadMore();check(wait([&]{return service.entered.load();}),"page held in flight");
    facade.setDialogFilter("no-match");QTest::qWait(220);service.release=true;
    check(wait([&]{return !model->loading();})&&model->rowCount()==0&&!model->hasMore(),"late old search page cannot leak into new query");
    service.delay=false;facade.setDialogFilter("");check(wait([&]{return !model->loading();})&&model->rowCount()==50,"search resets again");
    service.fail=true;const auto old=service.fallbacks.load();model->refreshPages();check(wait([&]{return !model->loading();})&&!model->pageError().isEmpty()&&model->rowCount()==50&&service.fallbacks==old,"unavailable does not downgrade or erase displayed list");
    service.fail=false;service.legacy=true;model->loadMore();check(wait([&]{return !model->loading();})&&model->rowCount()==1&&model->titleAt(0)=="Legacy"&&service.fallbacks==old+1,"only UNIMPLEMENTED uses legacy list");
  }
  server->Shutdown();std::cout<<"DIALOG_PAGE_UI_"<<(failures?"FAIL":"OK")<<'\n';return failures?1:0;
}
