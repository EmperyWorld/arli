#include "create_policy.hpp"
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
int main() {
  char tmp[] = "/tmp/arli-create-policy-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp);
  int failures = 0;
  auto check = [&](bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; ++failures; }
  };
  const auto scoped = arli::classify_turn_intent(
      "Создай result.txt с ровно пятью байтами ready. Других файлов не создавай.");
  auto gate = [&](const arli::TurnIntent& policy, const std::string& tool, nlohmann::json args) {
    return arli::creation_gate(policy, root, tool, args.dump()).ok;
  };
  check(scoped.forbid_create && scoped.allowed_create_paths.size() == 1, "scoped exclusion");
  check(gate(scoped, "create_file", {{"path", "./result.txt"}}), "explicit path allowed");
  for (const auto* path : {"other.txt", "../result.txt", "/tmp/result.txt", "Result.txt"}) {
    check(!gate(scoped, "create_file", {{"path", path}}), "other/escaping/case variant denied");
    check(!gate(scoped, "write_file", {{"filePath", path}}), "alias denied");
    for (const auto* key : {"dest", "destination", "to"})
      check(!gate(scoped, "copy_file", {{key, path}}), "copy destination denied");
    check(!gate(scoped, "move_file", {{"dest", path}}), "move destination denied");
    check(!gate(scoped, "restore_file", {{"path", path}}), "restore missing denied");
  }
  check(!gate(scoped, "run_shell", {{"command", "python3 -c pass"}}), "shell bypass denied");
  check(!gate(scoped, "start_background", {{"command", "touch other.txt"}}), "background bypass denied");
  check(arli::creation_gate(scoped,root,"run_shell",R"({"command":"sh ./verify.sh"})",true).ok,"read-only sandbox dispatch capability permits verification");
  std::ofstream(root / "existing.txt") << "old";
  check(gate(scoped, "write_file", {{"path", "existing.txt"}, {"append", true}}), "existing append allowed");
  const auto global = arli::classify_turn_intent("Создай result.txt. Не создавай новые файлы.");
  check(!gate(global, "create_file", {{"path", "result.txt"}}), "global prohibition wins");
  const auto conflict = arli::classify_turn_intent("Создай result.txt. Других файлов не создавай. Без новых файлов.");
  check(conflict.allowed_create_paths.empty(), "global plus scoped stays closed");
  for (const auto* prompt : {"Прочитай result.txt. Других файлов не создавай.",
                             "Не надо: создай result.txt. Других файлов не создавай.",
                             "Как создать result.txt? Других файлов не создавай."})
    check(arli::classify_turn_intent(prompt).allowed_create_paths.empty(), "no invented exception");
  const auto quoted = arli::classify_turn_intent("СОЗДАЙ файл \"Dir/My File.txt\". Других файлов не создавай.");
  check(gate(quoted, "create_file", {{"path", "Dir/My File.txt"}}), "quoted case preserved");
  check(gate(quoted, "make_dir", {{"path", "Dir"}}), "requested parent allowed");
  check(!gate(quoted, "make_dir", {{"path", "Other"}}), "unrequested directory denied");
  const auto english = arli::classify_turn_intent("Create file README. Do not create other files.");
  check(gate(english, "create_file", {{"path", "README"}}), "english extensionless file");
  fs::create_directory(root / "real");
  std::ofstream(root / "real/extra.txt") << "extra";
  check(!gate(scoped, "move_file", {{"src", "real"}, {"dest", "result.txt"}}), "directory move denied");
  fs::create_directory_symlink(root / "real", root / "Dir");
  check(!gate(quoted, "create_file", {{"path", "Dir/My File.txt"}}), "symlink exception denied");
  fs::remove_all(root);
  return failures ? 1 : 0;
}
