#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>

namespace mock_scenarios {
using json = nlohmann::json;
inline json call(const char* name, const json& args) {
  return {{"role", "assistant"}, {"content", nullptr}, {"tool_calls", json::array({json{
      {"id", "scenario_call"}, {"type", "function"}, {"function", {{"name", name}, {"arguments", args.dump()}}}}})}};
}
inline json done(const char* text = "done") { return {{"role", "assistant"}, {"content", text}}; }
inline std::optional<json> reply(const std::string& model, const json& messages) {
  int count = 0;
  for (const auto& m : messages) count += m.value("role", "") == "tool";
  if(model.rfind("procedure_",0)==0){
    const bool corrective=messages.dump().find("Результат не подтверждён")!=std::string::npos;
    if(model=="procedure_only")return count==0?call("run_shell",{{"command","sh ./verify.sh"}}):done();
    if(model=="procedure_read"){
      if(count==0||count==4||(count==6&&corrective))return call("read_file",{{"path","procedure.txt"}});
      if(count==1)return call("edit_file",{{"path","procedure.txt"},{"start_line",1},{"end_line",1},{"text","one"}});
      if(count==2)return call("read_file",{{"path","procedure.txt"},{"start_line",1},{"end_line",1}});
      if(count==3||count==5)return call("edit_file",{{"path","procedure.txt"},{"start_line",2},{"end_line",2},{"text","two"}});
      return done();
    }
    const bool stale=model=="procedure_stale";const int offset=stale?1:0;
    if(stale&&count==0)return call("run_shell",{{"command","sh ./verify.sh"}});
    if(count==offset)return call("read_file",{{"path","procedure.txt"}});
    if(count==offset+1)return call("edit_file",{{"path","procedure.txt"},{"start_line",1},{"end_line",1},{"text","changed"}});
    if((count==2&&(model=="procedure_fail"||model=="procedure_denied"||model=="procedure_no_create"))||(count==3&&model=="procedure_denied")||
       (corrective&&((stale&&count==3)||(model=="procedure_repair"&&count==2))))return call("run_shell",{{"command","sh ./verify.sh"}});
    return done();
  }
  if (model == "job_owner") {
    if(count==0)return call("start_background",{{"command","sleep 30"}});
    return done("background started");
  }
  if (model == "tool_read_secret") {
    if(count==0)return call("read_file",{{"path","README.md"}});
    for(const auto& m:messages)if(m.value("role","")=="tool"&&m.value("content","").find("fixture-llm-key-")!=std::string::npos)return done("SECRET_LEAK_TO_MODEL");
    return done("tool content clean");
  }
  if (model == "grpc_provider_write" || model == "grpc_provider_forbidden") {
    if (count == 0) return call(model == "grpc_provider_write" ? "grpc_write" : "grpc_forbidden", {{"title", "model request"}});
    return done("service completed");
  }
  if (model == "scoped_create") {
    if (count == 0) return call("create_file", {{"path", "result.txt"}, {"content", "ready"}});
    if (count == 1) return call("write_file", {{"path", "other.txt"}, {"content", "bad"}});
    if (count == 2) return call("copy_file", {{"src", "result.txt"}, {"dest", "copy-result.txt"}});
    if (count == 3) return call("read_file", {{"path", "result.txt"}});
    return done();
  }
  if (model == "acceptance_repair" || model == "acceptance_stuck") {
    if (count == 0) return call("read_file", {{"path", "acceptance.txt"}});
    if (count == 1) return call("edit_file", {{"path", "acceptance.txt"}, {"start_line", 1}, {"end_line", 2}, {"text", "1 DONE\n2 DONE"}});
    const bool corrective = messages.dump().find("строка 3 должна быть 3 DONE") != std::string::npos;
    if (model == "acceptance_repair" && corrective && count == 2) return call("read_file", {{"path", "acceptance.txt"}});
    if (model == "acceptance_repair" && corrective && count == 3)
      return call("edit_file", {{"path", "acceptance.txt"}, {"start_line", 3}, {"end_line", 3}, {"text", "3 DONE"}});
    return done("Everything done");
  }
  if (model == "large_reply") return done(std::string(3 * 1024 * 1024, 'x').c_str());
  if (model == "pinned_rules") {
    const auto system = messages.front().value("content", "");
    if (system.find("END_PINNED_RULE") == std::string::npos) return done("missing pinned rule");
    if (count == 0) return call("create_file", {{"path", "pinned.txt"}, {"content", "pinned applied"}});
    return done();
  }
  if (model == "scoped_rules") {
    const auto system = messages.front().value("content", "");
    if (system.find("ROOT_RULE") == std::string::npos) return done("missing root rules");
    if (system.find("NESTED_RULE") == std::string::npos)
      return call("create_file", {{"path", "nested/result.txt"}, {"content", "wrong"}});
    if (count <= 1) return call("create_file", {{"path", "nested/result.txt"}, {"content", "rules applied"}});
    return done();
  }
  if (model == "permission_once") {
    if (count == 0) return call("run_shell", {{"command", "echo arli-approved"}});
    return done();
  }
  if (model == "ask_twice") {
    for (const auto& message : messages)
      if (message.value("role", "") == "tool" && message.value("content", "") != "yes")
        return done("answer polluted");
    if (count < 2) return call("ask_user", {{"question", count == 0 ? "First?" : "Second?"}});
    return json{{"role", "assistant"}, {"content", "done"}};
  }
  if (model == "syntax_repair" || model == "syntax_stuck" || model == "syntax_inferred") {
    if (count == 0) return call("read_file", {{"path", "config.json"}});
    if (count == 1 && model == "syntax_inferred") return call("edit_file", {{"start_line", 1}, {"end_line", 1}, {"text", "{broken"}});
    if (count == 1) return call("edit_file", {{"path", "config.json"}, {"start_line", 1}, {"end_line", 1}, {"text", "{broken"}});
    if (model == "syntax_repair" && count == 2 && messages.back().value("role", "") == "user")
      return call("edit_file", {{"path", "config.json"}, {"start_line", 1}, {"end_line", 1}, {"text", "{\"value\":2}"}});
    return done("Я всё исправила, проверка проходит.");
  }
  if (model == "unread_edit" || model == "unread_range" || model == "write_twice") {
    if (count == 0 && model != "unread_edit") return call("read_file", {{"path", "target.txt"}, {"start_line", 1}, {"end_line", model == "write_twice" ? 2 : 1}});
    if ((count == 0 && model == "unread_edit") || count == 1)
      return call("edit_file", {{"path", "target.txt"}, {"start_line", model == "unread_range" ? 2 : 1}, {"end_line", model == "unread_range" ? 2 : 1}, {"text", "fixed"}});
    if (count == 2 && model == "write_twice") return call("edit_file", {{"path", "target.txt"}, {"start_line", 2}, {"end_line", 2}, {"text", "bad"}});
    return done();
  }
  if (model == "failed_check" || model == "check_stuck") {
    if (count == 0) return call("run_shell", {{"command", "/bin/sh -n target.sh"}});
    if (count == 1 && model == "failed_check") return call("read_file", {{"path", "target.sh"}});
    if (count == 2 && model == "failed_check")
      return call("edit_file", {{"path", "target.sh"}, {"search", "if then # broken"}, {"text", ": # fixed"}});
    if (!messages.empty() && messages.back().value("role", "") == "user" &&
        messages.back().value("content", "").find("Последняя проверка") != std::string::npos)
      return call("run_shell", {{"command", "/bin/sh -n target.sh"}});
    return done("Я всё исправила, проверка проходит.");
  }
  if (model == "json_actions") {
    if (count == 0) return done(json{{"action", "tool"}, {"name", "read_file"},
        {"params", {{"path", "target.txt"}}}}.dump().c_str());
    if (count == 1) return done(json{{"action", "tool"}, {"name", "edit_file"},
        {"params", {{"path", "target.txt"}, {"start_line", 1}, {"end_line", 1}, {"text", "fixed"}}}}.dump().c_str());
    return done(json{{"action", "final"}, {"answer", "done"}}.dump().c_str());
  }
  if (model == "raw_calls" || model == "raw_calls_stuck") {
    bool repair = false;
    for (const auto& m : messages)
      repair |= m.value("role", "") == "user" && m.value("content", "").find("Ни один из этих вызовов не исполнен") != std::string::npos;
    if (model == "raw_calls_stuck" || (count == 0 && !repair))
      return done("Изучу файл.\n<call:read_file{path:<|\"|>target.txt<|\"|>}/>");
    if (count == 0) return call("read_file", {{"path", "target.txt"}});
    if (count == 1) return call("edit_file", {{"path", "target.txt"}, {"start_line", 1}, {"end_line", 1}, {"new_text", "fixed"}});
    return done();
  }
  if (model == "repair_history") {
    if (count == 0) return call("read_file", {{"path", "target.txt"}});
    if (count == 1) {
      json result = call("append_file", json::object());
      result["tool_calls"][0]["function"]["arguments"] = R"({"path":"target.txt","content":"literal ,}",})";
      return result;
    }
    for (const auto& message : messages) {
      if (message.contains("tool_calls") && message["tool_calls"][0]["function"]["name"] == "append_file") {
        const auto& args = message["tool_calls"][0]["function"]["arguments"];
        if (!args.is_object() || args.value("content", "") != "literal ,}") return done("lost repaired arguments");
      }
    }
    return done();
  }
  if (model == "recover_args") {
    if (count == 0) return call("edit_file", {{"path", "target.txt"}, {"start_line", 1}, {"end_line", 1}});
    if (count == 1) return call("read_file", {{"path", "target.txt"}});
    if (count == 2) return call("edit_file", {{"path", "target.txt"}, {"start_line", 1}, {"end_line", 1}, {"new_text", "fixed"}});
    return done();
  }
  if (model == "readonly_attempt") {
    if (count == 0) return call("edit_file", {{"path", "target.txt"}, {"start_line", 1}, {"end_line", 1}, {"new_text", "bad"}});
    return done();
  }
  if (model == "review_queue") {
    switch (count) {
      case 0: return call("file_pass_set", {{"paths", {"target.txt", "dependency.txt"}}});
      case 1: return call("read_file", {{"path", "target.txt"}});
      case 2: return call("read_file", {{"path", "dependency.txt"}});
      case 3: return call("file_pass_review", {{"path", "target.txt"}, {"note", "already correct"}});
      case 4: return call("file_pass_review", {{"path", "dependency.txt"}, {"note", "dependency is correct"}});
      default: return done();
    }
  }
  if (model == "copy_success") {
    if (count == 0) return call("copy_file", {{"src", "target.txt"}, {"dest", "copy.txt"}});
    return done();
  }
  if (model == "long_mock") {
    if (messages.front().value("content", "").find("KEEP_CONSTRAINT") == std::string::npos) return done("lost task");
    const json* last = nullptr;
    for (const auto& m : messages) if (m.value("role", "") == "tool") last = &m;
    const std::string content = last ? last->value("content", "") : "";
    if (content.find("# target.txt lines") == std::string::npos)
      return call("read_file", {{"path", "target.txt"}});
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
      const auto bar = line.find('|');
      if (bar == std::string::npos || line.find(" KEEP") == std::string::npos) continue;
      const int number = std::stoi(line.substr(0, bar));
      return call("edit_file", {{"path", "target.txt"}, {"start_line", number}, {"end_line", number},
                               {"new_text", std::to_string(number) + " DONE"}});
    }
    return done();
  }
  return std::nullopt;
}
}  // namespace mock_scenarios
