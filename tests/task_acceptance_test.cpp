#include "task_acceptance.hpp"
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs = std::filesystem;
using json = nlohmann::json;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
  char tmp[] = "/tmp/arli-acceptance-XXXXXX"; if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp);
  auto check = json{{"kind", "line_range"}, {"path", "board.txt"}, {"expected", "{n} DONE"}, {"start_line", 1}, {"end_line", 16}};
  auto plan = json{{"checks", json::array({check})}, {"unverifiable", ""}};
  auto acceptance = arli::TaskAcceptance::from_json(plan);
  { std::ofstream out(root/"board.txt"); for (int i = 1; i <= 16; ++i) out << i << (i == 16 ? " KEEP\n" : " DONE\n"); }
  acceptance.mutation();
  CHECK(acceptance.evaluate(root).find("строка 16") != std::string::npos && acceptance.state() == "failed");
  { std::ofstream out(root/"board.txt"); for (int i = 1; i <= 16; ++i) out << i << " DONE\r\n"; }
  acceptance.mutation();
  CHECK(acceptance.evaluate(root).empty() && acceptance.state() == "checks_passed" && acceptance.checked_revision() == 2);
  acceptance.mutation(); CHECK(acceptance.state() == "unverified");
  std::ofstream(root/"board.txt") << "foreign";
  CHECK(!acceptance.evaluate(root).empty()); // Re-reads disk, no cached pass after an external edit.
  auto semantic=arli::TaskAcceptance::from_json(
      {{"checks",json::array({{{"kind","file_contains"},{"path","consumer.py"},{"expected","n == LIMIT"},
                               {"start_line",0},{"end_line",0}},
                              {{"kind","read_after_each_write"},{"path","consumer.py"},{"expected",""},
                               {"start_line",0},{"end_line",0}}})},{"unverifiable",""}},
      "allowed должна допускать n == LIMIT; после каждой правки перечитай файл");
  const auto semantic_plan=json::parse(semantic.serialized());
  CHECK(semantic_plan["checks"].size()==1&&semantic_plan["checks"][0]["kind"]=="read_after_each_write"&&
        !semantic.note().empty());
  auto final_read=arli::TaskAcceptance::from_json(
      {{"checks",json::array({{{"kind","read_after_each_write"},{"path","large.txt"},{"expected",""},
                               {"start_line",0},{"end_line",0}}})},{"unverifiable",""}},
      "После изменений проверь результат чтением файла");
  CHECK(json::parse(final_read.serialized())["checks"].empty()&&
        final_read.note().find("не требовалось")!=std::string::npos);
  for (const std::string kind : {"file_equals", "file_contains", "file_not_contains", "file_absent"}) {
    check = {{"kind",kind},{"path","board.txt"},{"expected",kind=="file_not_contains"?"missing":"foreign"}};
    auto condition = arli::TaskAcceptance::from_json({{"checks",json::array({check})},{"unverifiable","behavior not checked"}});
    CHECK(condition.evaluate(root).empty() == (kind != "file_absent"));
    if (kind != "file_absent") CHECK(condition.state()=="partial");
  }
  plan["checks"][0]["path"] = "../outside";
  auto invalid = arli::TaskAcceptance::from_json(plan);
  CHECK(invalid.evaluate(root).empty() && invalid.state() == "unverified");
  plan["checks"][0]["path"] = "board.txt"; plan["checks"][0]["end_line"] = 9999999999LL;
  CHECK(arli::TaskAcceptance::from_json(plan).state() == "unverified");
  CHECK(arli::TaskAcceptance::from_json({{"checks", {"bad"}}}).state() == "unverified");
  auto command=arli::TaskAcceptance::from_json({{"checks",json::array({{{"kind","command_succeeds"},{"path",""},{"expected","sh ./verify.sh"}}})},{"unverifiable",""}});
  CHECK(!command.evaluate(root).empty());command.observe(root,"run_shell",{{"command","sh ./verify.sh"}},{true,"started"},false);
  CHECK(!command.evaluate(root).empty());command.observe(root,"run_shell",{{"command","sh ./verify.sh"}},{true,"exit0"},true);
  CHECK(command.evaluate(root).empty()&&command.state()=="checks_passed");command.mutation();CHECK(!command.evaluate(root).empty());
  command.observe(root,"run_shell",{{"command","sh ./verify.sh"}},{false,"denied"},false,true);CHECK(command.blocked());
  auto order=arli::TaskAcceptance::from_json({{"checks",json::array({{{"kind","read_after_each_write"},{"path","./board.txt"},{"expected",""}}})},{"unverifiable",""}});
  order.observe(root,"edit_file",{{"path","board.txt"}},{true,"written"});CHECK(!order.before_tool(root,"edit_file",true).empty());
  order.observe(root,"read_file",{{"path","board.txt"}},{true,"page",{},{1,1,3,"version-a"}});CHECK(!order.evaluate(root).empty());
  order.observe(root,"read_file",{{"path","board.txt"}},{true,"page",{},{2,3,3,"version-b"}});CHECK(!order.evaluate(root).empty());
  order.observe(root,"read_file",{{"path",(root/"board.txt").string()}},{true,"page",{},{1,1,3,"version-b"}});CHECK(order.evaluate(root).empty()&&order.before_tool(root,"edit_file",true).empty());
  order.observe(root,"edit_file",{{"path","board.txt"}},{true,"written"});CHECK(!order.evaluate(root).empty());
  order.observe(root,"read_file",{{"path","board.txt"}},{true,"empty",{},{0,0,0,"empty"}});CHECK(order.evaluate(root).empty());
  fs::remove_all(root);
}
