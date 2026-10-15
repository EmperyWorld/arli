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
  std::atomic<int> calls{0};std::atomic<bool> delay{false},entered{false},release{false},fail{false},legacy{false};
  grpc::Status Health(grpc::ServerContext*,const arli::v1::HealthRequest*,arli::v1::HealthReply* r)override{r->set_serving(true);return grpc::Status::OK;}
  grpc::Status ListDialogs(grpc::ServerContext*,const arli::v1::ListDialogsRequest*,arli::v1::ListDialogsReply* r)override{r->add_dialogs()->set_id(1);return grpc::Status::OK;}
  grpc::Status OpenDialog(grpc::ServerContext*,const arli::v1::OpenDialogRequest* q,arli::v1::OpenDialogReply* r)override{
    r->mutable_dialog()->set_id(q->dialog_id());
    for(int n=51;n<=60;++n){auto* m=r->add_messages();m->set_id(n);m->set_role("user");m->set_text("dialog "+std::to_string(q->dialog_id()));}
    if(!legacy)r->set_next_before_id(51);return grpc::Status::OK;
  }
  grpc::Status ListMessages(grpc::ServerContext* ctx,const arli::v1::ListMessagesRequest* q,arli::v1::ListMessagesReply* r)override{
    ++calls;entered=true;
    while(delay&&!release&&!ctx->IsCancelled())std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(fail)return {grpc::StatusCode::UNAVAILABLE,"injected failure"};
    for(int n=1;n<q->before_id();++n){auto* m=r->add_messages();m->set_id(n);m->set_role("assistant");m->set_text("older "+std::to_string(n));}
    return grpc::Status::OK;
  }
};
int main(int argc,char** argv){
  QGuiApplication app(argc,argv);Service service;grpc::ServerBuilder b;int port=0;
  b.AddListeningPort("127.0.0.1:0",arli::auth::server_credentials("page-ui","page-internal"),&port);b.RegisterService(&service);auto server=b.BuildAndStart();if(!server)return 2;
  qputenv("ARLI_LISTEN",QByteArray("127.0.0.1:")+QByteArray::number(port));qputenv("ARLI_UI_TOKEN","page-ui");
  int failures=0;auto check=[&](bool yes,const char* why){if(!yes){++failures;std::cerr<<why<<'\n';}};
  auto wait=[&](auto fn){QElapsedTimer t;t.start();while(!fn()&&t.elapsed()<4000)QTest::qWait(10);return fn();};
  {MessageModel model;QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.setAll({{51,"user","new",0,false}});model.resetPaging(51);model.loadOlder();
    model.acceptOlder({{52,"user","out of order",0,false}},52,model.pageGeneration());
    check(model.messageCount()==1&&model.hasMore()&&!model.pageError().isEmpty(),"invalid page cannot mutate history");
    model.loadOlder();model.acceptOlder({{1,"user","old",0,false}},0,model.pageGeneration());
    check(model.messageCount()==2&&!model.hasMore()&&model.historyOpen(),"valid prepend obeys Qt model contract");}
  {AgentFacade facade;check(wait([&]{return facade.dialogId()==1;}),"opens first page");auto* model=facade.messages();
    check(model->messageCount()==10&&model->hasMore(),"older history advertised");
    service.delay=true;model->loadOlder();model->loadOlder();check(wait([&]{return service.entered.load();}),"request in flight");
    check(service.calls==1&&model->loadingOlder(),"duplicate click coalesced");
    facade.openDialog(2);check(wait([&]{return facade.dialogId()==2;}),"switch does not wait for history control request");
    service.release=true;check(wait([&]{return !model->loadingOlder();}),"new dialog independent loading state");QTest::qWait(100);
    check(model->messageCount()==10&&model->bodyAt(0)=="dialog 2","late page cannot contaminate new dialog");
    service.delay=false;service.fail=true;model->loadOlder();check(wait([&]{return !model->loadingOlder();}),"error releases loading state");
    check(model->hasMore()&&!model->pageError().isEmpty()&&model->messageCount()==10,"failed page retains retry boundary and messages");
    service.fail=false;model->loadOlder();check(wait([&]{return !model->loadingOlder();}),"retry completes");
    check(model->messageCount()==60&&!model->hasMore()&&model->bodyAt(0)=="older 1"&&model->bodyAt(50)=="dialog 2","prepend without losing existing messages");
    model->resetPaging(1);model->loadOlder();auto generation=model->pageGeneration();model->resetPaging(0);
    model->acceptOlder({{1,"user","stale",0,false}},0,generation);check(model->messageCount()==60,"generation fences repeated selection");
    service.legacy=true;facade.openDialog(3);check(wait([&]{return facade.dialogId()==3;}),"legacy reply opens");
    check(!model->hasMore()&&model->messageCount()==10,"legacy absence does not invent a cursor");
  }
  server->Shutdown();std::cout<<"MESSAGE_PAGE_UI_"<<(failures?"FAIL":"OK")<<'\n';return failures?1:0;
}
