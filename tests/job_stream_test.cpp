#include "lifecycle_harness.hpp"
#include "db.hpp"
#include "job_identity.hpp"
#include "redact.hpp"
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
using namespace lt;
namespace fs=std::filesystem;
void write_file(const fs::path& path,const std::string& text){
  int fd=open(path.c_str(),O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC,0600);if(fd<0)throw std::runtime_error("fixture file");
  size_t done=0;while(done<text.size()){auto n=write(fd,text.data()+done,text.size()-done);if(n<=0)throw std::runtime_error("fixture write");done+=n;}close(fd);
}
struct ReadResult {std::string text,position;bool gap=false,terminal=false;int code=-1;grpc::Status status;};
int main(int argc,char** argv){if(argc!=3)return 2;
  char tmp[]="/tmp/arli-job-stream-XXXXXX";if(!mkdtemp(tmp))return 2;setenv("ARLI_STATE_DIR",tmp,1);
  const fs::path root(tmp),wd=root/"workspace";fs::create_directories(wd);arli::Database db((root/"arli.db").string());db.init();
  arli::redact::enable_persistence(root/"redaction.fingerprints");const std::string secret="job-stream-private-secret-0123456789";arli::redact::remember(secret);
  auto fixture=[&](const std::string& text,bool running=false){auto dir=(root/"jobs/job-XXXXXX").string();if(!mkdtemp(dir.data()))throw std::runtime_error("fixture dir");
    const auto log=fs::path(dir)/"output.log";write_file(log,text);const auto id=db.add_job(running?getpid():0,"stream fixture",wd.string(),log.string(),running?arli::process_identity(getpid()):"");if(!running)db.finish_job(id,"exited",7);return std::pair{id,log};};
  // Terminate the fixture: an unterminated 'b' can be a prefix of a randomly
  // generated credential and is intentionally masked by Stream::finish().
  const std::string raw=std::string(8180,'a')+secret+"Готово ✅"+std::string(24000,'b')+"\n";auto [id,log]=fixture(raw);
  const auto ep="127.0.0.1:"+std::to_string(free_port());Proc daemon;
  const std::vector<std::pair<std::string,std::string>> env={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  daemon.start(argv[1],{ep},env);Client client(ep);if(!client.wait_health(8000))return 3;client.token=arli::secrets::load(root/"ui_token");
  auto read=[&](std::int64_t job,const std::string& after,bool first=false,bool authenticated=true,std::int64_t owner=0){
    grpc::ClientContext ctx;if(authenticated)client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(4));
    arli::v1::WatchJobLogRequest req;req.set_id(job);req.set_after_position(after);req.set_expected_dialog_id(owner);
    auto stream=client.stub->WatchJobLog(&ctx,req);arli::v1::JobLogChunk chunk;ReadResult out;
    while(stream->Read(&chunk)){LT_CHECK(chunk.text().size()<=1024*1024,"bounded stream chunk");out.text+=chunk.text();out.position=chunk.next_position();out.gap|=chunk.gap();out.terminal=chunk.terminal();out.code=chunk.exit_code_known()?chunk.exit_code():-1;
      if(first&&!chunk.text().empty()){ctx.TryCancel();break;}}
    out.status=stream->Finish();return out;
  };
  auto initial=read(id,"",true);LT_CHECK(!initial.text.empty()&&!initial.position.empty(),"first committed resumable chunk");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();daemon.start(argv[1],{ep},env);LT_CHECK(client.wait_health(8000),"daemon restart");
  auto rest=read(id,initial.position);const bool resumed=rest.status.ok()&&rest.terminal&&rest.code==7&&!rest.gap&&initial.text+rest.text==arli::redact::text(raw);
  if(!resumed)std::cerr<<"resume diagnostic: rpc="<<rest.status.error_code()<<" terminal="<<rest.terminal<<" exit="<<rest.code<<" gap="<<rest.gap<<" initial_bytes="<<initial.text.size()<<" rest_bytes="<<rest.text.size()<<" expected_bytes="<<arli::redact::text(raw).size()<<" bytes_equal="<<(initial.text+rest.text==arli::redact::text(raw))<<'\n';
  LT_CHECK(resumed,"signed position resumes exactly after restart with UTF-8/secret boundary intact");
  LT_CHECK(read(id,initial.position+"0").status.error_code()==grpc::StatusCode::INVALID_ARGUMENT,"tampered position rejected");
  auto [other,other_log]=fixture("other");LT_CHECK(read(other,initial.position).status.error_code()==grpc::StatusCode::INVALID_ARGUMENT,"cross-job cursor rejected");
  LT_CHECK(read(id,"",false,false).status.error_code()==grpc::StatusCode::UNAUTHENTICATED,"stream requires UI credential");
  LT_CHECK(read(id,"",false,true,999).status.error_code()==grpc::StatusCode::FAILED_PRECONDITION,"owner mismatch rejected");
  fs::rename(log,log.string()+".1");write_file(log,"next generation\n");rest=read(id,initial.position);
  LT_CHECK(rest.status.ok()&&!rest.gap&&initial.text+rest.text==arli::redact::text(raw)+"next generation\n","resume across one retained rotation");
  fs::remove(log.string()+".1");fs::rename(log,log.string()+".1");write_file(log,"last generation\n");rest=read(id,initial.position);
  LT_CHECK(rest.status.ok()&&rest.gap&&rest.text=="next generation\nlast generation\n","expired cursor explicitly reports gap and starts at retained data");
  write_file(log.string()+".1","before "+secret.substr(0,17));write_file(log,secret.substr(17)+" after\n");rest=read(id,"");
  LT_CHECK(rest.status.ok()&&rest.text=="before [redacted] after\n","credential spanning generations never reaches stream");
  // Eight non-reading clients occupy the bounded server slots, but health remains available.
  auto [active,active_log]=fixture(std::string(1048576,'x'),true);write_file(active_log.string()+".1",std::string(1048576,'y'));
  std::vector<std::unique_ptr<grpc::ClientContext>> contexts;
  std::vector<std::unique_ptr<grpc::ClientReader<arli::v1::JobLogChunk>>> streams;
  for(int n=0;n<8;++n){auto ctx=std::make_unique<grpc::ClientContext>();client.auth(*ctx);arli::v1::WatchJobLogRequest req;req.set_id(active);auto stream=client.stub->WatchJobLog(ctx.get(),req);stream->WaitForInitialMetadata();contexts.push_back(std::move(ctx));streams.push_back(std::move(stream));}
  LT_CHECK(read(active,"").status.error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,"stream concurrency capped at eight");
  LT_CHECK(client.wait_health(1000),"slow readers do not block health");
  for(auto& ctx:contexts)ctx->TryCancel();for(auto& stream:streams){arli::v1::JobLogChunk chunk;while(stream->Read(&chunk)){}stream->Finish();}streams.clear();contexts.clear();sleep_ms(150);
  grpc::ClientContext deadline;client.auth(deadline);deadline.set_deadline(std::chrono::system_clock::now()+std::chrono::milliseconds(200));
  auto [quiet,quiet_log]=fixture("",true);arli::v1::WatchJobLogRequest request;request.set_id(quiet);auto quiet_stream=client.stub->WatchJobLog(&deadline,request);arli::v1::JobLogChunk chunk;while(quiet_stream->Read(&chunk)){}
  LT_CHECK(quiet_stream->Finish().error_code()==grpc::StatusCode::DEADLINE_EXCEEDED,"quiet stream honors client deadline");
  grpc::ClientContext no_deadline;client.auth(no_deadline);request.set_id(active);auto slow=client.stub->WatchJobLog(&no_deadline,request);slow->WaitForInitialMetadata();
  const auto begin=std::chrono::steady_clock::now();sleep_ms(16000);while(slow->Read(&chunk)){}
  LT_CHECK(slow->Finish().error_code()==grpc::StatusCode::CANCELLED&&std::chrono::steady_clock::now()-begin<std::chrono::seconds(18),"server lease expires for non-reading client without deadline");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();
  std::cout<<"JOB_STREAM_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
