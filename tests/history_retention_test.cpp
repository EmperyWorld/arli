#include "db.hpp"
#include <filesystem>
#include <iostream>

int main(){
  char tmp[]="/tmp/arli-history-retention-XXXXXX";if(!mkdtemp(tmp))return 2;
  setenv("ARLI_STATE_DIR",tmp,1);arli::Database db((std::filesystem::path(tmp)/"state.sqlite").string());db.init();
  const auto dialog=db.create_dialog("history");std::vector<std::int64_t> tasks;
  for(int turn=1;turn<=3;++turn){
    auto task=db.enqueue_task(dialog.id,turn,"request","request-"+std::to_string(turn));tasks.push_back(task.id);
    db.mark_running(task.id,"worker");
    arli::StepRow step;step.dialog_id=dialog.id;step.turn_id=turn;step.task_id=task.id;step.kind="status";step.output="detail";
    db.add_step(step);db.add_message(dialog.id,"assistant","answer",turn);db.finish_task(task.id,"done","");
  }
  if(db.retain_history(1,90,128)!=2){std::cerr<<"retention count\n";return 1;}
  if(!db.list_steps_for_task(tasks[0]).empty()||!db.events_after(tasks[0],0).empty()){
    std::cerr<<"old payload retained\n";return 1;
  }
  if(db.list_steps_for_task(tasks[2]).empty()||db.events_after(tasks[2],0).empty()){
    std::cerr<<"recent payload removed\n";return 1;
  }
  if(db.list_messages(dialog.id).size()!=3||!db.get_task(tasks[0])){
    std::cerr<<"chat or task metadata removed\n";return 1;
  }
  if(db.retain_history(1,90,128)!=0){std::cerr<<"retention not idempotent\n";return 1;}
  std::filesystem::remove_all(tmp);std::cout<<"OK history-retention\n";return 0;
}
