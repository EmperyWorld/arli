#include "lifecycle_harness.hpp"
#include "db.hpp"
#include "task_limits.hpp"
#include <fstream>
using namespace lt;namespace fs=std::filesystem;
int main(int argc,char** argv){
  if(argc!=4)return 2;char tmp[]="/tmp/arli-admission-XXXXXX";if(!mkdtemp(tmp))return 2;setenv("ARLI_STATE_DIR",tmp,1);
  const auto root=fs::path(tmp),wd=root/"workspace";fs::create_directories(wd);const auto port=free_port();const std::vector<std::string> mock_args{std::to_string(port)};
  Proc mock,daemon;mock.start(argv[3],mock_args,{});const auto ep="127.0.0.1:"+std::to_string(free_port()),url="http://127.0.0.1:"+std::to_string(port)+"/v1";
  const std::vector<std::pair<std::string,std::string>> env={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  daemon.start(argv[1],{ep},env);Client client(ep);if(!client.wait_health(8000))return 3;client.token=arli::secrets::load(root/"ui_token");
  LT_CHECK(client.wait_worker("idle",8000)&&client.set_model(url,"ask_twice",wd.string(),120,3),"worker/model ready");
  arli::Database db((root/"arli.db").string());const auto dialog=client.create_dialog("holding");
  arli::v1::SendRequest original;original.set_dialog_id(dialog);original.set_text("ask twice");original.set_request_id("hold");
  grpc::ClientContext first;client.auth(first);first.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(10));auto initial=client.stub->Send(&first,original);arli::v1::TurnEvent event;int64_t task=0;bool asking=false;
  while(initial->Read(&event)){task=event.task_id();if(event.has_ask_user()){asking=true;break;}}first.TryCancel();while(initial->Read(&event)){}initial->Finish();LT_CHECK(asking&&task>0,"operator wait holds one running task after UI detaches");if(!asking)return 4;
  for(int n=1;n<arli::kMaxActiveTasks;++n){auto d=db.create_dialog("queued");db.enqueue_task(d.id,1,"queued","seed-"+std::to_string(n));}
  const auto target=client.create_dialog("new request");arli::v1::SendRequest request;request.set_dialog_id(target);request.set_text("new");request.set_request_id("new");
  auto rejected_send=[&](const char* prefix){grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));auto stream=client.stub->Send(&ctx,request);while(stream->Read(&event)){}auto status=stream->Finish();LT_CHECK(status.error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED&&status.error_message().find(prefix)!=std::string::npos,"Send reports specific capacity error");LT_CHECK(!db.task_by_request(target,"new"),"rejected Send has no task side effect");};
  auto rejected_revert=[&](const char* prefix){grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));arli::v1::RevertChangesetRequest req;req.set_dialog_id(target);req.set_request_id("undo");req.set_changeset_id("turn-00000000000000000000000000000000");auto stream=client.stub->RevertChangeset(&ctx,req);while(stream->Read(&event)){}auto status=stream->Finish();LT_CHECK(status.error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED&&status.error_message().find(prefix)!=std::string::npos,"revert shares capacity gate");LT_CHECK(!db.task_by_request(target,"undo"),"rejected revert has no task side effect");};
  rejected_send("TASK_QUEUE_CAPACITY");rejected_revert("TASK_QUEUE_CAPACITY");
  {grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));auto retry=client.stub->Send(&ctx,original);LT_CHECK(retry->Read(&event)&&event.task_id()==task,"dedup retry reattaches while queue is full");ctx.TryCancel();while(retry->Read(&event)){}retry->Finish();}
  std::vector<std::unique_ptr<grpc::ClientContext>> contexts;std::vector<std::unique_ptr<grpc::ClientReader<arli::v1::TurnEvent>>> streams;
  arli::v1::AttachTurnRequest attach;attach.set_dialog_id(dialog);attach.set_task_id(task);
  for(int n=0;n<arli::kMaxTurnStreams;++n){auto ctx=std::make_unique<grpc::ClientContext>();client.auth(*ctx);ctx->set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(30));auto stream=client.stub->AttachTurn(ctx.get(),attach);LT_CHECK(stream->Read(&event),"stream slot acquired");contexts.push_back(std::move(ctx));streams.push_back(std::move(stream));}
  rejected_send("TURN_STREAM_LIMIT");rejected_revert("TURN_STREAM_LIMIT");
  {grpc::ClientContext ctx;client.auth(ctx);auto extra=client.stub->AttachTurn(&ctx,attach);while(extra->Read(&event)){}LT_CHECK(extra->Finish().error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,"AttachTurn shares stream quota");}
  LT_CHECK(client.wait_health(1000),"control health available with all slots occupied");
  for(auto& ctx:contexts)ctx->TryCancel();for(auto& stream:streams){while(stream->Read(&event)){}stream->Finish();}streams.clear();contexts.clear();
  const auto queued=db.next_queued();db.finish_task(queued->id,"cancelled","free slot");
  bool admitted=false;
  for(int attempt=0;attempt<20&&!admitted;++attempt){
    grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));auto added=client.stub->Send(&ctx,request);admitted=added->Read(&event);
    if(admitted)ctx.TryCancel();while(added->Read(&event)){}const auto status=added->Finish();
    if(!admitted){LT_CHECK(status.error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,"cancelled slots release asynchronously, only capacity retry allowed");sleep_ms(25);}
  }
  LT_CHECK(admitted&&db.task_by_request(target,"new").has_value(),"terminal task and detached streams release capacity");
  for(const auto& row:db.list_tasks(0,200))if(row.id!=task&&!arli::task_status_terminal(row.status))db.finish_task(row.id,"cancelled","fixture cleanup");
  // Prepare while the worker is still held on the first task, before it can claim this fixture.
  const auto replay_dialog=db.create_dialog("large replay");const auto replay=db.enqueue_task(replay_dialog.id,1,"replay");
  for(int n=0;n<64;++n){arli::v1::TurnEvent chunk;chunk.set_dialog_id(replay_dialog.id);chunk.set_turn_id(1);chunk.mutable_token()->set_text(std::string(256*1024,'x')+"\n");db.append_event(replay.id,chunk);}db.finish_task(replay.id,"done");
  {grpc::ClientContext ctx;client.auth(ctx);arli::v1::CancelRequest req;req.set_dialog_id(dialog);arli::v1::CancelReply reply;LT_CHECK(client.stub->Cancel(&ctx,req,&reply).ok(),"cancel remains available");}
  LT_CHECK(client.wait_worker("idle",8000),"worker remains healthy");
  grpc::ChannelArguments slow_args;slow_args.SetMaxReceiveMessageSize(4*1024*1024);slow_args.SetInt(GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES,5);slow_args.SetInt(GRPC_ARG_HTTP2_BDP_PROBE,0);
  auto slow_stub=arli::v1::Agent::NewStub(grpc::CreateCustomChannel(ep,arli::auth::client_credentials(),slow_args));
  grpc::ClientContext slow_ctx;client.auth(slow_ctx);attach.set_dialog_id(replay_dialog.id);attach.set_task_id(replay.id);auto slow=slow_stub->AttachTurn(&slow_ctx,attach);slow->WaitForInitialMetadata();
  const auto started=std::chrono::steady_clock::now();sleep_ms(12000);LT_CHECK(client.wait_health(1000),"blocked Write does not block Health");while(slow->Read(&event)){}
  const auto slow_status=slow->Finish();if(slow_status.error_code()!=grpc::StatusCode::CANCELLED)std::cerr<<"slow stream status="<<slow_status.error_code()<<'\n';
  LT_CHECK(slow_status.error_code()==grpc::StatusCode::CANCELLED&&std::chrono::steady_clock::now()-started<std::chrono::seconds(16)&&db.get_task(replay.id)->status=="done","nonreading no-deadline client is cancelled without cancelling task");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();daemon.start(argv[1],{ep},env);client.reconnect(ep);LT_CHECK(client.wait_health(8000)&&client.wait_worker("idle",8000),"restart");
  {grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));auto retried=client.stub->Send(&ctx,original);while(retried->Read(&event))LT_CHECK(event.task_id()==task,"restart retry retains original task");LT_CHECK(retried->Finish().ok()&&db.task_by_request(dialog,"hold")->id==task,"restart dedup creates no replacement task");}
  LT_CHECK(client.set_model(url,"slow:11000",wd.string(),30,3)&&client.send(client.create_dialog("healthy"),"healthy").done(),"healthy wait longer than Write budget works after overload and restart");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();mock.term();if(mock.wait(3000)<0)mock.kill9();std::cout<<"TASK_ADMISSION_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
