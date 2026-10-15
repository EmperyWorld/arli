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
  const std::string id="turn-"+std::string(32,'a');std::string request;
  std::atomic<int> calls{0};std::atomic<bool> approved{false},resumed{false},delayed{false};
  grpc::Status Health(grpc::ServerContext*,const arli::v1::HealthRequest*,arli::v1::HealthReply* r)override{r->set_serving(true);r->set_event_resume_supported(true);return grpc::Status::OK;}
  grpc::Status ListDialogs(grpc::ServerContext*,const arli::v1::ListDialogsRequest*,arli::v1::ListDialogsReply* r)override{r->add_dialogs()->set_id(1);return grpc::Status::OK;}
  grpc::Status OpenDialog(grpc::ServerContext*,const arli::v1::OpenDialogRequest* q,arli::v1::OpenDialogReply* r)override{r->mutable_dialog()->set_id(q->dialog_id());auto* m=r->add_messages();m->set_id(1);m->set_turn_id(10);m->set_role("assistant");m->set_text("changed");return grpc::Status::OK;}
  grpc::Status GetTurnTrace(grpc::ServerContext*,const arli::v1::GetTurnTraceRequest* q,arli::v1::GetTurnTraceReply* r)override{
    if(q->turn_id()==20){delayed=true;std::this_thread::sleep_for(std::chrono::milliseconds(150));}
    auto* item=r->add_items();item->set_kind(q->turn_id()==21?"changeset":"done");item->set_args_json(q->turn_id()==11?"{}":"{\"changesetId\":\""+id+"\"}");return grpc::Status::OK;
  }
  grpc::Status ConfirmDanger(grpc::ServerContext*,const arli::v1::ConfirmDangerRequest* q,arli::v1::ConfirmDangerReply* r)override{approved=q->accept()&&q->confirm_token()=="approval";r->set_ok(approved);return grpc::Status::OK;}
  grpc::Status RevertChangeset(grpc::ServerContext* ctx,const arli::v1::RevertChangesetRequest* q,grpc::ServerWriter<arli::v1::TurnEvent>* w)override{
    const int n=++calls;arli::v1::TurnEvent e;e.set_dialog_id(1);e.set_turn_id(12);e.set_task_id(100);
    if(n==1){request=q->request_id();e.set_event_id(1);e.set_interaction_active(true);auto* c=e.mutable_confirm_needed();c->set_confirm_token("approval");c->set_command("revert_changeset "+id);c->set_consequence("Восстановить: original.txt");w->Write(e);return {grpc::StatusCode::UNAVAILABLE,"injected disconnect"};}
    resumed=q->source_turn_id()==10&&q->after_event_id()==1&&q->request_id()==request&&q->changeset_id()==id;
    while(!approved&&!ctx->IsCancelled())std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(ctx->IsCancelled())return grpc::Status::CANCELLED;
    e.set_event_id(2);e.mutable_done()->set_outcome("done");e.mutable_done()->set_assistant_text("reverted");w->Write(e);return grpc::Status::OK;
  }
};
int main(int argc,char** argv){
  QGuiApplication app(argc,argv);Service service;grpc::ServerBuilder b;int port=0;b.AddListeningPort("127.0.0.1:0",arli::auth::server_credentials("ui-fixture","internal-fixture"),&port);b.RegisterService(&service);auto server=b.BuildAndStart();if(!server)return 2;
  qputenv("ARLI_LISTEN",QByteArray("127.0.0.1:")+QByteArray::number(port));qputenv("ARLI_UI_TOKEN","ui-fixture");
  int failures=0;auto check=[&](bool yes,const char* why){if(!yes){++failures;std::cerr<<why<<'\n';}};
  auto wait=[&](auto fn){QElapsedTimer t;t.start();while(!fn()&&t.elapsed()<4000)QTest::qWait(10);return fn();};
  {AgentFacade facade;check(wait([&]{return facade.dialogId()==1;}),"dialog opens");
    facade.showTurn(20);check(wait([&]{return service.delayed.load();}),"trace in flight");facade.showTurn(20);QTest::qWait(200);check(facade.shownTurn()==0&&facade.messageSteps().empty(),"late trace cannot reopen collapsed selection");
    facade.showTurn(11);QTest::qWait(100);check(facade.messageSteps().empty(),"legacy trace exposes no undo");
    facade.showTurn(21);check(wait([&]{return !facade.messageSteps().empty();}),"crash changeset metadata discovered without Done");
    facade.showTurn(10);check(wait([&]{return !facade.messageSteps().empty();}),"changeset discovered from historical Done");
    facade.revertTurn("turn-"+QString(32,'b'));check(!facade.busy()&&service.calls==0,"only selected changeset accepted");
    facade.setReadOnly(true);facade.revertTurn(QString::fromStdString(service.id));check(!facade.busy()&&service.calls==0,"read-only guard");facade.setReadOnly(false);
    facade.setDraft("keep draft");QElapsedTimer timer;timer.start();facade.revertTurn(QString::fromStdString(service.id));check(timer.elapsed()<100&&facade.busy(),"revert queues without blocking GUI");
    check(wait([&]{return facade.confirmOpen()&&service.resumed.load();}),"reconnect preserves request ID and applied event cursor");check(facade.confirmKind()=="revert","dedicated confirmation label");
    facade.confirmDanger(true);check(wait([&]{return !facade.busy()&&service.approved.load();}),"control approval works during stream");check(service.calls==2&&!facade.confirmOpen()&&facade.draft()=="keep draft","no extra enqueue and draft retained");
  }
  server->Shutdown();std::cout<<"REVERT_UI_"<<(failures?"FAIL":"OK")<<'\n';return failures?1:0;
}
