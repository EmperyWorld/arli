#include "lifecycle_harness.hpp"
#include "db.hpp"
#include <filesystem>
#include <set>
using namespace lt;namespace fs=std::filesystem;
int main(int argc,char** argv){
  if(argc!=3)return 2;char tmp[]="/tmp/arli-message-page-XXXXXX";if(!mkdtemp(tmp))return 2;
  setenv("ARLI_STATE_DIR",tmp,1);const auto root=fs::path(tmp),file=root/"arli.db";
  arli::Database db(file.string());db.init();const auto dialog=db.create_dialog("history"),other=db.create_dialog("other");
  std::set<int64_t> expected;
  for(int n=0;n<425;++n){expected.insert(db.add_message(dialog.id,n%2?"assistant":"user","message "+std::to_string(n),n+1).id);if(n%30==0)db.add_message(dialog.id,"context_state","hidden");}
  db.add_message(other.id,"user","foreign");
  const auto ep="127.0.0.1:"+std::to_string(free_port());
  const std::vector<std::pair<std::string,std::string>> env={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  Proc daemon;daemon.start(argv[1],{ep},env);Client client(ep);if(!client.wait_health(8000))return 3;
  client.token=arli::secrets::load(root/"ui_token");
  auto page=[&](int64_t id,int64_t before,int size,arli::v1::ListMessagesReply& reply){
    grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
    arli::v1::ListMessagesRequest req;req.set_dialog_id(id);req.set_before_id(before);req.set_page_size(size);
    return client.stub->ListMessages(&ctx,req,&reply);
  };
  {grpc::ClientContext ctx;arli::v1::ListMessagesRequest req;req.set_dialog_id(dialog.id);arli::v1::ListMessagesReply reply;
    LT_CHECK(client.stub->ListMessages(&ctx,req,&reply).error_code()==grpc::StatusCode::UNAUTHENTICATED,"history requires UI bearer");}
  int64_t cursor=0;std::set<int64_t> seen;
  {grpc::ClientContext ctx;client.auth(ctx);arli::v1::OpenDialogRequest req;req.set_dialog_id(dialog.id);req.set_message_page_size(50);arli::v1::OpenDialogReply reply;
    LT_CHECK(client.stub->OpenDialog(&ctx,req,&reply).ok()&&reply.messages_size()==50&&reply.next_before_id()>0,"open returns first bounded page");
    cursor=reply.next_before_id();for(const auto& m:reply.messages())seen.insert(m.id());}
  db.add_message(dialog.id,"assistant","new insertion between pages");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();daemon.start(argv[1],{ep},env);LT_CHECK(client.wait_health(8000),"restart before continuation");
  int pages=0;
  while(cursor>0&&pages++<20){arli::v1::ListMessagesReply reply;const auto status=page(dialog.id,cursor,37,reply);
    LT_CHECK(status.ok()&&reply.messages_size()<=37,"bounded continuation");if(!status.ok())break;
    int64_t prev=0;for(const auto& m:reply.messages()){LT_CHECK(m.id()>prev&&m.id()<cursor&&seen.insert(m.id()).second&&m.role()!="context_state","stable ascending scoped nonduplicate message");prev=m.id();}
    cursor=reply.next_before_id();}
  LT_CHECK(cursor==0&&seen==expected,"all 425 messages exactly once despite new insertion and restart");
  {grpc::ClientContext ctx;client.auth(ctx);arli::v1::OpenDialogRequest req;req.set_dialog_id(dialog.id);arli::v1::OpenDialogReply reply;
    LT_CHECK(client.stub->OpenDialog(&ctx,req,&reply).ok()&&reply.messages_size()==200,"legacy OpenDialog still returns latest 200");}
  arli::v1::ListMessagesReply reply;
  LT_CHECK(page(dialog.id,-1,50,reply).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"negative boundary rejected");
  LT_CHECK(page(dialog.id,0,101,reply).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"oversized page rejected");
  LT_CHECK(page(999999,0,50,reply).error_code()==grpc::StatusCode::NOT_FOUND,"missing dialog rejected");
  const auto big=db.create_dialog("byte budget");db.add_message(big.id,"user",std::string(600*1024,'x'));db.add_message(big.id,"assistant","small");
  LT_CHECK(page(big.id,0,100,reply).ok()&&reply.messages_size()==1&&reply.next_before_id()>0,"byte budget shortens page without skipping next row");
  const auto next=reply.next_before_id();reply.Clear();LT_CHECK(page(big.id,next,100,reply).ok()&&reply.messages_size()==1&&reply.messages(0).text().size()==600*1024&&reply.next_before_id()==0,"single large item fits bounded transport");
  db.add_message(big.id,"assistant",std::string(3*1024*1024,'z'));reply.Clear();
  LT_CHECK(page(big.id,0,1,reply).error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,"oversized individual item explicitly fails, never truncates");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();std::cout<<"MESSAGE_PAGE_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
