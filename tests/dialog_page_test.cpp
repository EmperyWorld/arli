#include "lifecycle_harness.hpp"
#include "db.hpp"
#include <filesystem>
#include <set>
using namespace lt;namespace fs=std::filesystem;
int main(int argc,char** argv){
  if(argc!=3)return 2;char tmp[]="/tmp/arli-dialog-page-XXXXXX";if(!mkdtemp(tmp))return 2;
  setenv("ARLI_STATE_DIR",tmp,1);const fs::path root(tmp);arli::Database db((root/"arli.db").string());db.init();
  std::set<int64_t> expected;for(int n=0;n<1005;++n)expected.insert(db.create_dialog("dialog "+std::to_string(n)).id);
  db.set_dialog_instructions(*expected.begin(),"instructions belong only in OpenDialog");
  const auto ep="127.0.0.1:"+std::to_string(free_port());
  const std::vector<std::pair<std::string,std::string>> env={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  Proc daemon;daemon.start(argv[1],{ep},env);Client client(ep);if(!client.wait_health(8000))return 3;client.token=arli::secrets::load(root/"ui_token");
  auto page=[&](const std::string& token,int size,arli::v1::ListDialogPageReply& reply){
    grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
    arli::v1::ListDialogPageRequest req;req.set_page_token(token);req.set_page_size(size);return client.stub->ListDialogPage(&ctx,req,&reply);
  };
  {grpc::ClientContext ctx;arli::v1::ListDialogPageReply reply;LT_CHECK(client.stub->ListDialogPage(&ctx,arli::v1::ListDialogPageRequest(),&reply).error_code()==grpc::StatusCode::UNAUTHENTICATED,"page requires UI bearer");}
  std::string token;std::set<int64_t> seen;std::pair<std::string,int64_t> previous{"~",INT64_MAX};int count=0;bool valid=true;
  do{arli::v1::ListDialogPageReply reply;const auto status=page(token,37,reply);if(!status.ok()){valid=false;break;}
    for(const auto& row:reply.dialogs()){const auto order=std::make_pair(row.updated_at(),row.id());valid&=order<previous&&seen.insert(row.id()).second&&row.instructions().empty();previous=order;}
    token=reply.next_page_token();if(++count==1){daemon.term();if(daemon.wait(8000)<0)daemon.kill9();daemon.start(argv[1],{ep},env);valid&=client.wait_health(8000);}
  }while(!token.empty()&&count<40);
  LT_CHECK(valid&&token.empty()&&seen==expected,"all 1005 recency-ordered summaries exactly once across restart");
  {grpc::ClientContext ctx;client.auth(ctx);arli::v1::ListDialogsReply reply;LT_CHECK(client.stub->ListDialogs(&ctx,arli::v1::ListDialogsRequest(),&reply).error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,"legacy oversized list explicitly refused, not truncated");}
  arli::Database writer((root/"arli.db").string());
  auto changed=[&](auto mutate){arli::v1::ListDialogPageReply first,second;const auto status=page("",2,first);mutate();return status.ok()&&!first.next_page_token().empty()&&page(first.next_page_token(),2,second).error_code()==grpc::StatusCode::ABORTED;};
  LT_CHECK(changed([&]{writer.rename_dialog(*expected.begin(),"renamed");}),"rename invalidates continuation even in same timestamp second");
  LT_CHECK(changed([&]{writer.add_message(*expected.begin(),"user","touch");}),"worker message touch invalidates continuation");
  LT_CHECK(changed([&]{writer.create_dialog("new");}),"insert invalidates continuation");
  LT_CHECK(changed([&]{writer.delete_dialog(*expected.rbegin());}),"delete invalidates continuation");
  arli::v1::ListDialogPageReply reply;
  LT_CHECK(page("{}",50,reply).error_code()==grpc::StatusCode::INVALID_ARGUMENT&&page(std::string(513,'x'),50,reply).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"malformed/large page token rejected");
  LT_CHECK(page("",101,reply).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"page count bounded");
  {grpc::ClientContext ctx;client.auth(ctx);arli::v1::CreateDialogRequest req;req.set_title(std::string(4097,'x'));arli::v1::Dialog reply;
    LT_CHECK(client.stub->CreateDialog(&ctx,req,&reply).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"new title byte limit");}
  writer.create_dialog(std::string(200*1024,'x'));writer.create_dialog(std::string(200*1024,'y'));writer.create_dialog(std::string(200*1024,'z'));
  reply.Clear();LT_CHECK(page("",100,reply).ok()&&reply.dialogs_size()==2&&!reply.next_page_token().empty(),"legacy large titles respect soft page byte budget");
  writer.create_dialog(std::string(256*1024+1,'q'));reply.Clear();LT_CHECK(page("",1,reply).error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,"oversized legacy title explicitly refused");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();std::cout<<"DIALOG_PAGE_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
