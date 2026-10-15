#include "lifecycle_harness.hpp"
#include "turn.hpp"
#include "confirm.hpp"
#include <fstream>
using namespace lt;namespace fs=std::filesystem;
int main(int argc,char** argv){
  if(argc!=2)return 2;char tmp[]="/tmp/arli-procedure-XXXXXX";if(!mkdtemp(tmp))return 2;setenv("ARLI_STATE_DIR",tmp,1);
  const auto wd=fs::path(tmp)/"workspace";fs::create_directories(wd);arli::Database db((fs::path(tmp)/"arli.db").string());db.init();
  const auto port=free_port();const std::vector<std::string> args{std::to_string(port)};Proc mock;mock.start(argv[1],args,{});
  arli::TurnConfig cfg;cfg.workdir=wd.string();cfg.llm_base_url="http://127.0.0.1:"+std::to_string(port)+"/v1";cfg.max_steps=20;cfg.shell_auto.clear();cfg.shell_ask="sh";sleep_ms(100);
  for(const std::string model:{"procedure_fail","procedure_omitted","procedure_repair","procedure_stale","procedure_denied","procedure_only","procedure_read","procedure_no_create"}){
    cfg.llm_model=model;std::ofstream(wd/"procedure.txt")<<(model=="procedure_read"?"old\nold\nlast\n":"old\n");std::ofstream(wd/"verify.sh")<<(model=="procedure_fail"?"exit 7\n":model=="procedure_no_create"?"touch forbidden.txt\n":"exit 0\n");
    const auto dialog=db.create_dialog(model);Collected result;int approvals=0;
    const std::string prompt=model=="procedure_only"?"Запусти sh ./verify.sh. Других файлов не создавай.":
        model=="procedure_read"?"Измени procedure.txt. После каждой правки полностью перечитай файл. Других файлов не создавай.":
        "Измени procedure.txt и проверь выполнение требований. Других файлов не создавай.";
    arli::run_turn(db,cfg,dialog.id,prompt,1,[&](const auto& event){
      result.events.push_back(event);if(event.has_confirm_needed()){
        ++approvals;LT_CHECK(event.confirm_needed().command()=="sh ./verify.sh","only expected command approved");
        arli::ConfirmHub::instance().decide(dialog.id,event.confirm_needed().confirm_token(),model!="procedure_denied");
      }return true;
    },{});
    const bool success=model=="procedure_repair"||model=="procedure_stale"||model=="procedure_only"||model=="procedure_read";
    LT_CHECK(result.has([&](const auto& e){return e.has_done()&&e.done().outcome()==(success?"done":"failed")&&e.done().verification_state()==(success?"checks_passed":"failed");}),(model+" honest outcome").c_str());
    if(model=="procedure_denied")LT_CHECK(approvals==1,"denied exact command does not prompt repeatedly");
    if(model=="procedure_stale")LT_CHECK(approvals==2,"post-mutation revision requires command rerun");
    if(model=="procedure_read")LT_CHECK(result.has([](const auto& e){return e.has_observation()&&!e.observation().ok()&&e.observation().output().find("ACCEPTANCE_ORDER")!=std::string::npos;}),"partial reread blocks next mutation");
    if(model=="procedure_no_create")LT_CHECK(approvals==1&&!fs::exists(wd/"forbidden.txt")&&result.has([](const auto& e){return e.has_observation()&&e.observation().tool()=="run_shell"&&!e.observation().ok()&&e.observation().output().find("Read-only file system")!=std::string::npos;}),"no-create policy enforces readonly filesystem in actual shell");
  }
  mock.term();if(mock.wait(3000)<0)mock.kill9();std::cout<<"PROCEDURE_TURN_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
