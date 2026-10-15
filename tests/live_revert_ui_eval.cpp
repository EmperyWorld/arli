// Opt-in actual model + daemon/worker + Qt facade + restart + confirmed undo.
#include "lifecycle_harness.hpp"
#include "facade.hpp"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTest>
#include <nlohmann/json.hpp>
#include <fstream>
using namespace lt;namespace fs=std::filesystem;using json=nlohmann::json;
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
  if(argc!=6)return 2;QGuiApplication app(argc,argv);
  char tmp[]="/tmp/arli-live-revert-ui-XXXXXX";if(!mkdtemp(tmp))return 2;
  const fs::path root(tmp),wd=root/"workspace";fs::create_directories(wd);
  const std::string original="def add(a, b):\n    return a - b\n\n# KEEP\n",fixed="def add(a, b):\n    return a + b\n\n# KEEP\n";
  std::ofstream(wd/"calc.py")<<original;const auto ep="127.0.0.1:"+std::to_string(free_port());
  qputenv("ARLI_STATE_DIR",tmp);qputenv("ARLI_LISTEN",ep.c_str());
  const std::vector<std::pair<std::string,std::string>> env={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  Proc daemon;daemon.start(argv[1],{ep},env);Client client(ep);
  auto wait=[&](auto fn,int ms){QElapsedTimer time;time.start();while(!fn()&&time.elapsed()<ms)QTest::qWait(20);return fn();};
  json report={{"model",argv[4]},{"workspace",root.string()},{"through_daemon",true},{"through_qt_facade",true},{"passed",false}};
  try{
    auto require=[](bool yes,const char* why){if(!yes)throw std::runtime_error(why);};
    require(client.wait_health(8000),"daemon health");client.token=arli::secrets::load(root/"ui_token");qputenv("ARLI_UI_TOKEN",client.token.c_str());
    require(client.wait_worker("idle",8000)&&client.set_model(argv[3],argv[4],wd.string(),120,5),"configure actual model");const auto dialog=client.create_dialog("Qt live revert");
    {AgentFacade facade;require(wait([&]{return facade.dialogId()==dialog;},8000),"Qt opens dialog");
      facade.setDraft(QStringLiteral("Прочитай calc.py. Исправь add: она должна складывать a и b, сейчас вычитает. Сохрани все прочие строки и комментарий KEEP. Перечитай результат. Не создавай файлы. Shell не нужен."));facade.send();
      require(wait([&]{if(facade.confirmOpen())facade.confirmDanger(false);if(facade.askOpen())facade.cancel();return !facade.busy();},240000),"model turn deadline");
      require(read(wd/"calc.py")==fixed,"actual model changed exact requested bytes");report["model_disk_verified"]=true;
    }
    daemon.term();if(daemon.wait(8000)<0)daemon.kill9();daemon.start(argv[1],{ep},env);client.reconnect(ep);require(client.wait_health(8000)&&client.wait_worker("idle",8000),"daemon restart");
    const auto other=wd/"other-project";fs::create_directories(other);std::ofstream(other/"calc.py")<<"unrelated";
    require(client.set_model(argv[3],argv[4],other.string(),120,5),"switch current workspace before historical revert");report["workspace_changed_before_revert"]=true;
    {AgentFacade facade;require(wait([&]{return facade.dialogId()==dialog;},8000),"Qt reopens after restart");facade.showTurn(1);
      require(wait([&]{return !facade.messageSteps().empty();},8000),"historical steps loaded");QString id;
      for(const auto& step:facade.messageSteps()){auto row=step.toMap();if(row.value("kind")==QStringLiteral("changeset"))id=row.value("changeset").toString();}
      require(!id.isEmpty(),"durable model changeset discovered in Qt");report["changeset_id"]=id.toStdString();facade.revertTurn(id);
      require(wait([&]{return facade.confirmOpen()||!facade.busy();},10000)&&facade.confirmOpen(),"Qt receives revert approval");
      require(facade.confirmKind()=="revert"&&facade.confirmCommand()=="revert_changeset "+id&&facade.confirmConsequence().contains("calc.py")&&read(wd/"calc.py")==fixed,"reviewable confirmation before mutation");
      facade.confirmDanger(true);require(wait([&]{return !facade.busy();},15000)&&read(wd/"calc.py")==original,"approved Qt revert restores exact original");require(read(other/"calc.py")=="unrelated","current unrelated workspace preserved");report["revert_disk_verified"]=true;
      facade.showTurn(2);require(wait([&]{for(const auto& x:facade.messageSteps())if(x.toMap().value("kind")==QStringLiteral("changeset"))return true;return false;},8000),"undo journal discoverable in history");report["undo_journal_visible"]=true;
    }
    report["passed"]=true;
  }catch(const std::exception& e){report["error"]=e.what();}
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();std::ofstream(argv[5])<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';return report["passed"].get<bool>()?0:1;
}
