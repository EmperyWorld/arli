#include "lifecycle_harness.hpp"
#include "db.hpp"
#include "memory.hpp"
#include "jobs.hpp"
#include "tools.hpp"
#include <fstream>
#include <future>
#include <iostream>
namespace fs=std::filesystem;
using json=nlohmann::json;
int main(){
 char tmp[]="/tmp/arli-full-audit-XXXXXX"; if(!mkdtemp(tmp))return 2;
 const fs::path root(tmp),wd=root/"workspace"; fs::create_directories(wd);
 setenv("ARLI_STATE_DIR",tmp,1);
 json report; report["date"]="2026-10-10";
 {
 arli::Database db((root/"unit.sqlite").string());db.init();auto d=db.create_dialog("history");
 for(int i=1;i<=405;++i)db.add_message(d.id,"user",std::to_string(i),i);
 auto rows=db.list_messages(d.id,400);
 report["history"]={{"inserted",405},{"returned",rows.size()},{"first",rows.front().text},{"last",rows.back().text}};
 std::vector<arli::MemoryRow> mem;
 for(int i=0;i<30;++i){arli::MemoryRow m;m.text="fact"+std::to_string(i);mem.push_back(m);}
 db.replace_memory(mem);auto memory=arli::MemoryState::load(arli::memory_rows_for_inject(db));
 memory.remember({{"text","new fact"}}); db.replace_memory(memory.entries());
 report["memory"]={{"before",30},{"after_remember",db.list_memory(200).size()}};
 setenv("ARLI_INTERNAL_TOKEN","AUDIT_SYNTHETIC_MARKER",1);
 auto env=arli::tool_run_shell(wd,"printenv ARLI_INTERNAL_TOKEN");
 report["shell_inherits_token"] = env.ok && env.output.find("AUDIT_SYNTHETIC_MARKER")!=std::string::npos;
 const std::string write="python3 -c 'open(\"marker.txt\",\"w\").write(\"audit\")'";
 report["readonly_lexer_allows_python_write"]=!arli::shell_mutates_files(write);
 auto written=arli::tool_run_shell(wd,write);
 report["allowed_python_writes_workspace"]=written.ok&&fs::exists(wd/"marker.txt");
 const std::string outside=(root/"outside-workdir.txt").string();
 auto bg=arli::execute_shell(db,wd,"python3 -c 'open(\""+outside+"\",\"w\").write(\"audit\")'",{},true,{});
 for(int i=0;i<40&&!fs::exists(outside);++i)lt::sleep_ms(50);
 report["background_escapes_workdir_with_default_sandbox_settings"]=bg.ok&&fs::exists(outside);
 unsetenv("ARLI_INTERNAL_TOKEN");
 }
 lt::Proc daemon,mock,second;const std::string bin="/data/repos/arli/build-ci/";
 int port=lt::free_port();auto url="http://127.0.0.1:"+std::to_string(port)+"/v1";
 mock.start(bin+"arli-mock-llm",{std::to_string(port)},{});
 auto ep="127.0.0.1:"+std::to_string(lt::free_port());
 std::vector<std::pair<std::string,std::string>> vars={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",bin+"arli-agent"},{"ARLI_LISTEN",ep}};
 daemon.start(bin+"arli-daemon",{ep},vars);lt::Client c(ep);
 if(!c.wait_health(8000)){daemon.kill9();mock.kill9();return 3;}
 c.token=arli::secrets::load(root/"ui_token"); c.wait_worker("idle",8000);
 std::ofstream(wd/"config.json")<<"{\"value\":1}\n";
 c.set_model(url,"syntax_stuck",wd.string(),600,5); auto d=c.create_dialog("false terminal success");
 auto sent=c.send(d,"Исправь config.json: value должна быть 2");
 report["failed_verification_terminal"]={{"error_event",sent.error_contains("Проверка не прошла")},{"task_status",c.last_task(d).status()}};
 {grpc::ClientContext ctx;c.auth(ctx);arli::v1::ConfirmDangerRequest req;req.set_dialog_id(d);req.set_confirm_token("not-a-real-confirm");req.set_accept(true);arli::v1::ConfirmDangerReply rep;auto st=c.stub->ConfirmDanger(&ctx,req,&rep);report["invalid_confirm_reports_accepted"]=st.ok()&&rep.ok();}
 {grpc::ClientContext ctx;c.auth(ctx);arli::v1::AnswerAskUserRequest req;req.set_dialog_id(d);req.set_answer("stale");arli::v1::AnswerAskUserReply rep;auto st=c.stub->AnswerAskUser(&ctx,req,&rep);report["answer_without_question_reports_success"]=st.ok()&&rep.ok();}
 c.set_model(url,"slow:5000",wd.string(),600,5); auto d2=c.create_dialog("second daemon");
 auto active=std::async(std::launch::async,[&]{return c.send(d2,"hi",10000);});
 for(int i=0;i<60 && c.last_task(d2).status()!="running";++i)lt::sleep_ms(50);
 report["second_daemon_before"]=c.last_task(d2).status();
 second.start(bin+"arli-daemon",{ep},vars);report["second_daemon_exit"]=second.wait(3000);
 auto task=c.last_task(d2);report["second_daemon_after"]={{"status",task.status()},{"error",task.error()}};
 auto ended=active.get();(void)ended;
 daemon.term();if(daemon.wait(8000)<0)daemon.kill9();second.kill9();mock.kill9();
 std::ofstream("/tmp/arli-full-audit-probe.json")<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';
 fs::remove_all(root);return 0;
}
