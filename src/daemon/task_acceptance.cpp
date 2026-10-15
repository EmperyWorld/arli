#include "task_acceptance.hpp"
#include "llm_internal.hpp"
#include "tools.hpp"
#include "tools/file_io.hpp"
#include "shell_policy.hpp"
#include <sstream>
#include <set>
#include <algorithm>
#include <cctype>
namespace arli {
namespace {
using json = nlohmann::json;
const std::set<std::string> kinds = {"file_equals", "file_contains", "file_not_contains", "file_absent", "line_range", "command_succeeds", "read_after_each_write"};
json schema() {
  return json::parse(R"json([{"type":"function","function":{"name":"record_acceptance",
    "description":"Record immutable file acceptance checks before execution.","parameters":{"type":"object",
    "required":["checks","unverifiable"],"properties":{
      "unverifiable":{"type":"string","description":"Requirements not covered by these checks; empty only when all are covered."},
      "checks":{"type":"array","maxItems":64,"items":{"type":"object",
        "required":["kind","path","expected","start_line","end_line"],"properties":{
          "kind":{"type":"string","enum":["file_equals","file_contains","file_not_contains","file_absent","line_range","command_succeeds","read_after_each_write"]},
          "path":{"type":"string"},"expected":{"type":"string"},
          "start_line":{"type":"integer"},"end_line":{"type":"integer"}
        }}}
    }}}}])json");
}
std::string expected_line(std::string text, int line) {
  for (size_t pos = 0; (pos = text.find("{n}", pos)) != std::string::npos;) {
    const auto value = std::to_string(line); text.replace(pos, 3, value); pos += value.size();
  }
  return text;
}
}
TaskAcceptance TaskAcceptance::from_json(const json& original, const std::string& request) {
  TaskAcceptance result;
  try {
    json value=original;
    if(!request.empty()&&value.is_object()&&value.contains("checks")&&value["checks"].is_array()){
      std::string lower=request;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){
        return static_cast<char>(c>='A'&&c<='Z'?c-'A'+'a':c);
      });
      const bool exact=lower.find("буквальн")!=std::string::npos||lower.find("Буквальн")!=std::string::npos||
          lower.find("точный текст")!=std::string::npos||lower.find("Точный текст")!=std::string::npos||
          lower.find("должен содержать")!=std::string::npos||lower.find("должна содержать")!=std::string::npos||
          lower.find("exact text")!=std::string::npos||lower.find("must contain")!=std::string::npos;
      const bool each_write=(lower.find("после каждой правк")!=std::string::npos||lower.find("После каждой правк")!=std::string::npos)||
          lower.find("после каждого изменен")!=std::string::npos||
          lower.find("После каждого изменен")!=std::string::npos||
          lower.find("после каждого изменён")!=std::string::npos||
          lower.find("После каждого изменён")!=std::string::npos||
          lower.find("after each write")!=std::string::npos||
          lower.find("after every write")!=std::string::npos||
          lower.find("after each edit")!=std::string::npos;
      bool removed_expression=false,removed_reread=false;
      auto& checks=value["checks"];
      checks.erase(std::remove_if(checks.begin(),checks.end(),[&](const json& item){
          if(!item.is_object()||!item.contains("kind")||!item.contains("expected"))return false;
          const auto kind=item.value("kind","");
          if(kind=="read_after_each_write"&&!each_write){removed_reread=true;return true;}
          if(exact)return false;
          if(kind!="file_contains"&&kind!="file_not_contains")return false;
          const auto expected=item.value("expected","");
          const bool expression=expected.find('=')!=std::string::npos||expected.find('<')!=std::string::npos||
              expected.find('>')!=std::string::npos||expected.find("&&")!=std::string::npos||
              expected.find("||")!=std::string::npos;
          removed_expression=removed_expression||expression;return expression;
        }),checks.end());
      if(removed_expression||removed_reread){
        auto note=value.value("unverifiable","");
        if(!note.empty())note+=" ";
        if(removed_expression)note+="Смысловые выражения кода не проверяются как обязательные буквальные фрагменты. ";
        if(removed_reread)note+="Полное чтение после каждой записи не требовалось пользователем.";
        value["unverifiable"]=std::move(note);
      }
    }
    if (!value.is_object() || !value.contains("checks") || !value["checks"].is_array() ||
        value["checks"].size() > 64 || value.dump().size() > 65536 || !value.contains("unverifiable") ||
        !value["unverifiable"].is_string()) return result;
    for (const auto& item : value["checks"]) {
      if (!item.is_object() || !item.contains("kind") || !item["kind"].is_string() ||
          !item.contains("path") || !item["path"].is_string() || !item.contains("expected") || !item["expected"].is_string()) return result;
      const auto path = item["path"].get<std::string>();
      const auto kind = item["kind"].get<std::string>();
      if (!kinds.count(kind)) return result;
      if(kind=="command_succeeds"){
        const auto command=item["expected"].get<std::string>();
        if(!path.empty()||command.empty()||command.size()>8192||command.find('\0')!=std::string::npos||
            shell_policy::masks_failure_status(command))return result;
        continue;
      }
      if (path.empty() || path.find('\0') != std::string::npos || std::filesystem::path(path).is_absolute()) return result;
      for (const auto& part : std::filesystem::path(path)) if (part == "..") return result;
      if (kind == "line_range") {
        if (!item.contains("start_line") || !item["start_line"].is_number_integer() ||
            !item.contains("end_line") || !item["end_line"].is_number_integer()) return result;
        const auto start = item["start_line"].get<std::int64_t>(), end = item["end_line"].get<std::int64_t>();
        if (start < 1 || end < start || end > 1000000 || end - start > 2048 || item["expected"].get<std::string>().find('\n') != std::string::npos) return result;
      }
      if ((kind == "file_contains" || kind == "file_not_contains") && item["expected"].get<std::string>().empty()) return result;
    }
    result.plan_ = value;
    for(const auto& item:value["checks"]){
      if(item["kind"]=="command_succeeds")result.commands_[item["expected"].get<std::string>()]=-1;
      if(item["kind"]=="read_after_each_write")result.rereads_[std::filesystem::path(item["path"].get<std::string>()).lexically_normal().generic_string()]={};
    }
    result.note_ = value["unverifiable"].get<std::string>();
    if (value["checks"].empty() && result.note_.empty()) result.note_ = "Нет проверяемых критериев.";
  } catch (...) { return TaskAcceptance{}; }
  return result;
}
TaskAcceptance TaskAcceptance::prepare(const LlmConfig& cfg, const std::string& request, std::atomic<bool>* cancel) {
  const auto tools = schema();
  const json messages = json::array({{{"role", "system"}, {"content",
    "ARLI_ACCEPTANCE_PLAN. До выполнения задания зафиксируй только проверяемые требования пользователя вызовом record_acceptance. "
    "Не выполняй задание. Не выдумывай файлы, код, тесты или точный текст для смыслового требования. "
    "file_equals сравнивает все байты; file_contains/file_not_contains проверяют буквальный фрагмент; file_absent отсутствие файла. "
    "line_range проверяет каждую строку start_line..end_line включительно, expected использует {n} для номера строки, без перевода строки. "
    "Диапазоны проверяй целиком, включая последний элемент. Конечное содержимое файла проверяемо: обязательно включай его в checks. "
    "Например, запрос заменить строки 2..7 файла sample.txt на '<номер> OK' требует "
    "{kind:line_range,path:sample.txt,expected:'{n} OK',start_line:2,end_line:7}. "
    "command_succeeds: path='', expected содержит точную команду, явно указанную пользователем или правилами проекта. "
    "Требует успешного foreground run_shell после последней правки. Не выдумывай команд и не считай запуск background завершённой проверкой. "
    "read_after_each_write: path — конкретный файл, expected=''; требует полный read_file после каждой правки, до следующей мутации. "
    "Если просят перечитывать board.txt после каждой правки, добавь read_after_each_write для board.txt вместе с файловыми критериями. "
    "Общий порядок действий и неизвестные пути указывай в unverifiable. Составной запрос разделяй на проверяемую и непроверяемую части. "
    "Ограничения, которые этими проверками не доказать, перечисли в unverifiable. "
    "Не объявляй смысловую корректность кода доказанной наличием текста. Фраза «функция должна допускать n == LIMIT» "
    "задаёт поведение: нельзя добавлять file_contains с n == LIMIT, потому что эквивалентная реализация может быть n <= LIMIT. "
    "Если явных критериев нет, верни checks=[] и причину. "
    "Это отдельный анализ задания, не ответ пользователю."}}, {{"role", "user"}, {"content", request}}});
  TaskAcceptance result;
  try {
    const auto reply = chat_completions(cfg, messages, tools, "auto", cancel);
    if (!reply.ok || !reply.message.contains("tool_calls") || !reply.message["tool_calls"].is_array()) return result;
    const auto& calls = reply.message["tool_calls"];
    if (calls.size() != 1 || calls[0]["function"].value("name", "") != "record_acceptance") return result;
    auto args = calls[0]["function"]["arguments"];
    if (args.is_string()) args = json::parse(args.get<std::string>(), nullptr, false);
    return from_json(args,request);
  } catch (...) { return result; }
}
std::string TaskAcceptance::evaluate(const std::filesystem::path& root) {
  failure_.clear(); checked_revision_ = revision_;
  for (const auto& check : plan_["checks"]) {
    const auto path = check["path"].get<std::string>(), kind = check["kind"].get<std::string>();
    const auto expected = check["expected"].get<std::string>();
    if(kind=="command_succeeds"){
      if(commands_.at(expected)!=revision_)failure_="Требуется успешная команда после последней правки: "+expected;
      if(!failure_.empty())break;continue;
    }
    if(kind=="read_after_each_write"){
      if(rereads_.at(std::filesystem::path(path).lexically_normal().generic_string()).pending)failure_="После правки требуется полный read_file: "+path;
      if(!failure_.empty())break;continue;
    }
    try {
      const auto file = resolve_under(root, path);
      if (kind == "file_absent") {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(std::filesystem::weakly_canonical(root) / path, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) throw std::runtime_error(ec.message());
        if (std::filesystem::exists(status)) failure_ = path + ": файл должен отсутствовать";
      } else {
        const auto bytes = tools_internal::read_file_bytes(file);
        if (kind == "file_equals" && bytes != expected) failure_ = path + ": содержимое не равно ожидаемому";
        if (kind == "file_contains" && bytes.find(expected) == std::string::npos) failure_ = path + ": отсутствует ожидаемый фрагмент " + expected;
        if (kind == "file_not_contains" && bytes.find(expected) != std::string::npos) failure_ = path + ": остался запрещённый фрагмент " + expected;
        if (kind == "line_range") {
          const int start = check["start_line"], end = check["end_line"];
          std::istringstream lines(bytes); std::string line; int number = 0;
          while (number < end && std::getline(lines, line)) {
            ++number; if (!line.empty() && line.back() == '\r') line.pop_back();
            if (number >= start && line != expected_line(expected, number)) {
              failure_ = path + ": строка " + std::to_string(number) + " должна быть " + expected_line(expected, number); break;
            }
          }
          if (failure_.empty() && number < end) failure_ = path + ": отсутствует строка " + std::to_string(end);
        }
      }
    } catch (const std::exception& e) { failure_ = path + ": " + e.what(); }
    if (!failure_.empty()) break;
  }
  return failure_;
}
std::string TaskAcceptance::state() const {
  if (!failure_.empty()) return "failed";
  if (checked_revision_ != revision_ || plan_["checks"].empty()) return "unverified";
  return note_.empty() ? "checks_passed" : "partial";
}
}
