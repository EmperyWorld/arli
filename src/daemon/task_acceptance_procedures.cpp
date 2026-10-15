#include "task_acceptance.hpp"
#include "turn_policy.hpp"
namespace arli {
namespace fs=std::filesystem;using json=nlohmann::json;
std::string TaskAcceptance::before_tool(const fs::path&,const std::string&,bool mutating)const{
  if(mutating)for(const auto& [path,read]:rereads_)if(read.pending)return "ACCEPTANCE_ORDER: сначала полностью перечитай read_file "+path+" после предыдущей правки";
  return {};
}
void TaskAcceptance::observe(const fs::path& root,const std::string& tool,const json& args,const ToolResult& result,bool foreground_shell_ran,bool shell_refused){
  if(tool=="run_shell"&&args.contains("command")&&args["command"].is_string()){
    const auto command=args["command"].get<std::string>();
    if(shell_refused)blocked_commands_.insert(command);
    if(auto it=commands_.find(command);it!=commands_.end())it->second=result.ok&&foreground_shell_ran?revision_:-1;
  }
  if(!result.ok)return;
  auto relative=[&](const std::string& name){fs::path path(name);if(path.is_absolute())path=path.lexically_relative(fs::absolute(root).lexically_normal());return path.lexically_normal().generic_string();};
  if(tool=="move_file")for(const auto* key:{"src","source","from"})if(args.contains(key)&&args[key].is_string()){
    if(auto source=rereads_.find(relative(args[key].get<std::string>()));source!=rereads_.end()){source->second=ReadState{};source->second.pending=true;}break;
  }
  std::string name=coerce_path_arg(args);
  if(tool=="copy_file"||tool=="move_file")for(const auto* key:{"dest","destination","to"})if(args.contains(key)&&args[key].is_string()){name=args[key].get<std::string>();break;}
  if(name.empty())return;
  const auto it=rereads_.find(relative(name));if(it==rereads_.end())return;
  auto& read=it->second;
  if(tool_writes_workdir(tool)){read=ReadState{};read.pending=true;return;}
  if(tool!="read_file"||!read.pending||result.read.total<0||result.read.hash.empty())return;
  const auto& coverage=result.read;
  if(read.hash!=coverage.hash||read.total!=coverage.total){read.lines.clear();read.hash=coverage.hash;read.total=coverage.total;}
  for(int n=coverage.first;n<=coverage.last&&n>0;++n)read.lines.insert(n);
  if(read.lines.size()==static_cast<size_t>(read.total))read.pending=false;
}
}
