// Opt-in real model, authenticated daemon/worker, exact harmless shell approval.
#include "lifecycle_harness.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
using namespace lt;namespace fs=std::filesystem;using json=nlohmann::json;
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
  if(argc!=6)return 2;char tmp[]="/tmp/arli-live-procedure-XXXXXX";if(!mkdtemp(tmp))return 2;
  const fs::path root(tmp),wd=root/"workspace";fs::create_directories(wd);
  const std::string script="test \"$(cat result.txt)\" = ready || exit 7\nprintf 'procedure-check-ok\\n'\n";
  std::ofstream(wd/"result.txt")<<"old\n";std::ofstream(wd/"verify.sh")<<script;
  const auto ep="127.0.0.1:"+std::to_string(free_port());
  const std::vector<std::pair<std::string,std::string>> env={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  Proc daemon;daemon.start(argv[1],{ep},env);Client client(ep);
  json report={{"model",argv[4]},{"workspace",root.string()},{"through_daemon",true},{"passed",false}};
  try{
    auto require=[](bool yes,const char* why){if(!yes)throw std::runtime_error(why);};
    require(client.wait_health(8000),"daemon health");client.token=arli::secrets::load(root/"ui_token");
    require(client.wait_worker("idle",8000)&&client.set_model(argv[3],argv[4],wd.string(),120,5),"configure actual model");
    const auto dialog=client.create_dialog("procedural acceptance");grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(240));
    arli::v1::SendRequest req;req.set_dialog_id(dialog);req.set_request_id("procedure-live");
    req.set_text("Прочитай result.txt и замени его содержимое на ровно ready и один перевод строки. После каждой правки result.txt полностью перечитай этот файл. После правок выполни точную команду sh ./verify.sh и убедись в её успехе. verify.sh не меняй. Других файлов не создавай.");
    auto stream=client.stub->Send(&ctx,req);arli::v1::TurnEvent event;json plan;bool done=false,command_ok=false;int approvals=0;
    while(stream->Read(&event)){
      if(event.has_confirm_needed()&&event.interaction_active()){
        const bool accept=event.confirm_needed().command()=="sh ./verify.sh"&&read(wd/"verify.sh")==script;
        grpc::ClientContext answer_ctx;client.auth(answer_ctx);answer_ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
        arli::v1::ConfirmDangerRequest answer;answer.set_dialog_id(dialog);answer.set_confirm_token(event.confirm_needed().confirm_token());answer.set_accept(accept);arli::v1::ConfirmDangerReply reply;
        require(client.stub->ConfirmDanger(&answer_ctx,answer,&reply).ok()&&reply.ok(),"confirmation delivery");if(accept)++approvals;
      }
      if(event.has_observation()&&event.observation().tool()=="run_shell")command_ok|=event.observation().ok()&&event.observation().output().find("procedure-check-ok")!=std::string::npos;
      if(event.has_done()){
        done=event.done().outcome()=="done";report["verification_state"]=event.done().verification_state();report["checked_revision"]=event.done().checked_revision();report["changeset_id"]=event.done().changeset_id();
        if(!event.done().verification_plan_json().empty())plan=json::parse(event.done().verification_plan_json());
      }
    }
    require(stream->Finish().ok()&&done,"successful terminal outcome");
    bool command=false,reread=false,file=false;for(const auto& check:plan.at("checks")){
      command|=check["kind"]=="command_succeeds"&&check["expected"]=="sh ./verify.sh";
      reread|=check["kind"]=="read_after_each_write"&&check["path"]=="result.txt";
      file|=check["kind"]=="file_equals"&&check["path"]=="result.txt"&&check["expected"]=="ready\n";
    }
    report["plan"]=plan;report["approvals"]=approvals;report["command_observed"]=command_ok;
    require(command&&reread&&file,"model plan covers all three explicit requirements");
    size_t entries=0;for(const auto& entry:fs::directory_iterator(wd)){
      require(!entry.is_symlink()&&entry.is_regular_file()&&(entry.path().filename()=="result.txt"||entry.path().filename()=="verify.sh"),"no unrelated files created");++entries;
    }
    require(entries==2&&read(wd/"result.txt")=="ready\n"&&read(wd/"verify.sh")==script&&command_ok&&approvals>0&&
        (report["verification_state"]=="checks_passed"||report["verification_state"]=="partial"),"independent bytes, shell and verification result");
    report["script_preserved_verified"]=true;report["no_extra_files_verified"]=true;
    report["passed"]=true;
  }catch(const std::exception& e){report["error"]=e.what();}
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();std::ofstream(argv[5])<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';return report["passed"].get<bool>()?0:1;
}
