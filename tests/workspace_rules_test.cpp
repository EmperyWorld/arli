#include "workspace_rules.hpp"
#include "context.hpp"
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs = std::filesystem;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
  char tmp[]="/tmp/arli-rules-XXXXXX"; if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp); fs::create_directories(root/"nested/deep");
  std::ofstream(root/"AGENTS.md") << "ROOT_KEEP";
  std::ofstream(root/"nested/AGENTS.md") << "NESTED_KEEP";
  std::ofstream(root/"nested/deep/AGENTS.md") << "DEEP_KEEP";
  arli::WorkspaceRules rules(root);
  CHECK(rules.refresh().changed && rules.render().find("ROOT_KEEP") != std::string::npos);
  CHECK(rules.render().find("NESTED_KEEP") == std::string::npos);
  auto update = rules.for_tool("create_file", {{"path","nested/deep/result.txt"}});
  CHECK(update.ok && update.changed && rules.render().find("DEEP_KEEP") != std::string::npos);
  CHECK(!rules.for_tool("create_file", {{"path","nested/deep/result.txt"}}).changed);
  CHECK(!rules.refresh({"../escape"}).ok);
  CHECK(!rules.refresh({"/tmp/escape"}).ok);
  fs::create_directory_symlink(root/"nested",root/"link");
  CHECK(!rules.refresh({"link/result.txt"}).ok);
  fs::remove(root/"nested/deep/AGENTS.md");
  CHECK(rules.refresh().changed && rules.render().find("DEEP_KEEP") == std::string::npos);
  std::ofstream(root/"nested/AGENTS.md") << "NEW_RULES";
  CHECK(rules.refresh().changed && rules.render().find("NEW_RULES") != std::string::npos);
  arli::WorkspaceRules next_turn(root);
  CHECK(next_turn.for_tool("list_dir", {{"path","nested"}}).ok);
  CHECK(next_turn.render().find("NEW_RULES") != std::string::npos);
  std::ofstream(root/"nested/file.txt") << "ready";
  arli::WorkspaceRules file_search(root);
  CHECK(file_search.for_tool("rg_search", {{"path","nested/file.txt"}}).ok);
  CHECK(file_search.render().find("NEW_RULES")!=std::string::npos);
  CHECK(file_search.for_tool("rg_search", {{"path","nested/file.txt/invalid-child"}}).ok);
  fs::create_symlink(root/"nested/file.txt",root/"file-link");
  CHECK(!file_search.for_tool("rg_search", {{"path","file-link"}}).ok);
  auto messages = nlohmann::json::array({nlohmann::json{{"role","system"},{"content",rules.render()}}});
  const auto pinned = messages.front();
  for (int i=0;i<30;++i) messages.push_back({{"role",i%2?"user":"assistant"},{"content",std::string(1000,'x')}});
  arli::context::fit_budget(messages,4096);
  CHECK(messages.front()==pinned);
  std::ofstream(root/"AGENTS.md") << std::string(33000,'x');
  CHECK(!rules.refresh().ok);
  fs::remove(root/"AGENTS.md"); fs::create_symlink(root/"nested/AGENTS.md",root/"AGENTS.md");
  CHECK(!rules.refresh().ok);
  fs::remove_all(root);
}
