#include "turn_policy.hpp"
#include "create_policy.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <string>
#include <sstream>

namespace arli {
namespace {

std::string lower_ru(std::string s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(std::tolower(c)));
      ++i;
      continue;
    }
    if (c == 0xD0 && i + 1 < s.size()) {
      const unsigned char d = static_cast<unsigned char>(s[i + 1]);
      if (d == 0x81) {
        out.push_back(static_cast<char>(0xD1));
        out.push_back(static_cast<char>(0x91));
        i += 2;
        continue;
      }
      if (d >= 0x90 && d <= 0x9F) {
        out.push_back(static_cast<char>(0xD0));
        out.push_back(static_cast<char>(d + 0x20));
        i += 2;
        continue;
      }
      if (d >= 0xA0 && d <= 0xAF) {
        out.push_back(static_cast<char>(0xD1));
        out.push_back(static_cast<char>(d - 0x20));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

bool contains_ci(const std::string& hay, const char* needle) {
  return lower_ru(hay).find(lower_ru(needle)) != std::string::npos;
}

bool any_of_ci(const std::string& hay, std::initializer_list<const char*> needles) {
  for (const char* n : needles) {
    if (contains_ci(hay, n)) {
      return true;
    }
  }
  return false;
}

}  // namespace

TurnIntent classify_turn_intent(const std::string& user_text, const std::string& previous_answer) {
  // A numeric reply selects the offered item, not an unrelated new text-only request.
  const auto begin = user_text.find_first_not_of(" \t\r\n");
  const auto end = user_text.find_last_not_of(" \t\r\n");
  if (begin != std::string::npos && begin == end && user_text[begin] >= '1' && user_text[begin] <= '9') {
    const std::string number(1, user_text[begin]);
    std::istringstream lines(previous_answer); std::string line;
    while (std::getline(lines, line)) {
      const auto pos = line.find_first_not_of(" \t");
      if (pos != std::string::npos && (line.compare(pos, 2, number + ".") == 0 || line.compare(pos, 2, number + ")") == 0))
        return classify_turn_intent(line.substr(pos + 2));
    }
  }
  const std::string text = lower_ru(user_text);
  auto has = [&](std::initializer_list<const char*> phrases) { return any_of_ci(text, phrases); };
  auto word = [&](const char* candidate) {
    const std::string value(candidate);
    auto letter = [](unsigned char c) { return c >= 128 || std::isalnum(c) || c == '_'; };
    for (size_t pos = text.find(value); pos != std::string::npos; pos = text.find(value, pos + 1)) {
      if ((pos == 0 || !letter(text[pos - 1])) &&
          (pos + value.size() == text.size() || !letter(text[pos + value.size()]))) return true;
    }
    return false;
  };
  auto words = [&](std::initializer_list<const char*> values) {
    for (const auto* value : values) if (word(value)) return true;
    return false;
  };
  TurnIntent intent;
  intent.forbid_mutation = has({"не изменяй файлы", "не меняй файлы", "не редактируй", "без изменения файлов",
                               "без правок", "только анализ", "только объясни", "do not edit", "don't edit",
                               "do not modify", "read only", "read-only", "analysis only"});
  classify_creation_policy(intent, user_text, text);
  const bool explanation = has({"как исправить", "как изменить", "как добавить", "как создать", "как улучшить",
                                "как работает", "что можно сделать", "почему", "how to", "how do", "what is"}) ||
                           words({"объясни", "объяснение", "расскажи", "посоветуй", "explain", "describe"});
  const bool prose = words({"стих", "стихотворение", "рассказ", "письмо", "ответ", "poem", "story"});
  const bool path_hint = has({"файл", "проект", "репозитор", "на диск", "file", "project", "repository",
                             ".cpp", ".qml", ".py", ".md", ".txt", ".js", ".html", ".css", ".json"});
  const bool edit_action = words({"исправь", "исправить", "исправляй", "измени", "изменить", "изменяй",
                                 "добавь", "добавить", "добавляй", "удали", "удалить", "удаляй", "замени", "заменить",
                                 "переименуй", "перенеси", "скопируй", "обнови", "обновить", "редактируй", "отредактируй", "дописать",
                                 "перепиши", "переписать", "улучши", "улучшим", "улучшать", "улучшить", "доработай", "доработаем",
                                 "доработать", "расширь", "расширять", "вноси", "правь", "fix", "edit", "refactor",
                                 "implement", "modify", "update", "delete", "rename", "replace"});
  const bool generic_action = words({"делай", "сделай", "сделать", "продолжай", "продолжаем", "продолжить", "работаем", "приступай"});
  const bool create_action = words({"напиши", "напишем", "пиши", "создай", "создать", "write", "create", "add"});
  const bool deliver_file = create_action && has({"в файл", "на диск", "save to", "write to", "create file", "создай файл"});
  const bool explicit_after_analysis = has({"и исправь", "затем исправь", "потом исправь", "и измени", "and fix", "and edit"});
  const bool vague_project_improvement = has({"улучшим проект", "улучши проект", "улучшить проект",
                                               "доработаем проект", "доработай проект", "improve project"}) &&
      !has({"ошиб", "баг", "дефект", "функц", "экран", "интерфейс", "тест", "сборк", "производительн",
            "безопас", "grpc", "инструмент", "редакт", "файл ", ".cpp", ".qml", ".py", ".js", ".html",
            ".css", ".json", "error", "bug", "test", "performance", "security"});
  intent.needs_clarification = !intent.forbid_mutation && vague_project_improvement;
  intent.expects_mutation = !intent.forbid_mutation && (!explanation || explicit_after_analysis || deliver_file) &&
                           (!prose || deliver_file) && (edit_action || (generic_action && !wants_inventory_list(text)) || (create_action && path_hint));
  intent.requires_tools = intent.expects_mutation || wants_single_file_read(text) || wants_inventory_list(text) ||
                          words({"изучи", "проверь", "посмотри", "запусти", "собери", "выполни", "проверить",
                                 "inspect", "check", "run", "build", "test"});
  if ((prose && !deliver_file) || (explanation && !path_hint && !explicit_after_analysis)) intent.requires_tools = false;
  if (intent.needs_clarification) intent.requires_tools = true;
  return intent;
}

bool wants_file_work(const std::string& user_text) {
  return classify_turn_intent(user_text).expects_mutation;
}

bool wants_single_file_read(const std::string& user_text) {
  if (any_of_ci(user_text, {"посмотри файл", "открой файл", "прочитай", "содержимое файла",
                            "что внутри", "покажи файл", "покажи код", "read file", "read the",
                            "open file", "show file", "quote_file", "read_file"})) {
    return true;
  }
  if (contains_ci(user_text, "посмотри") && contains_ci(user_text, "файл") &&
      !any_of_ci(user_text, {"какие файлы", "список файлов", "перечисли файлы"})) {
    return true;
  }
  // «файл foo.cpp» / «файл X» with a path hint — not inventory-only.
  if (contains_ci(user_text, "файл ") && (user_text.find('.') != std::string::npos ||
                                           any_of_ci(user_text, {".cpp", ".h", ".js", ".html", ".css", ".py", ".md"}))) {
    return true;
  }
  return false;
}

bool wants_inventory_list(const std::string& user_text) {
  if (any_of_ci(user_text, {"список файлов", "какие файлы", "перечисли файлы", "перечисли",
                            "файлы проекта", "файлы в ", "структур проекта", "структуру проекта",
                            "инвентар", "что сделано", "что осталось", "что готово",
                            "осталось сделать", "list files", "file tree", "project tree"})) {
    return true;
  }
  if (contains_ci(user_text, "список") &&
      any_of_ci(user_text, {"файл", "папк", "каталог", "директор", "проект"})) {
    return true;
  }
  if (any_of_ci(user_text, {"какие папки", "список папок", "папки в", "папк проекта"})) {
    return true;
  }
  return false;
}

bool is_mutation_tool(const std::string& tool_name) {
  return tool_name == "edit_file" || tool_name == "replace_text" || tool_name == "batch_replace" || tool_name == "create_file" || tool_name == "append_file" ||
         tool_name == "write_file";
}

bool tool_writes_workdir(const std::string& tool_name) {
  if (is_mutation_tool(tool_name)) {
    return true;
  }
  return tool_name == "make_dir" || tool_name == "copy_file" || tool_name == "move_file" ||
         tool_name == "delete_file" || tool_name == "delete_dir" || tool_name == "restore_file";
}

bool is_explore_tool(const std::string& tool_name) {
  return tool_name == "read_file" || tool_name == "rg_search" || tool_name == "list_dir";
}

std::string tool_call_key(const std::string& tool_name, const std::string& args_json) {
  std::string key = tool_name;
  try {
    const auto args = nlohmann::json::parse(args_json.empty() ? "{}" : args_json);
    key += "|args=" + args.dump();
  } catch (...) {
    key += "|raw=";
    key += args_json;
  }
  return key;
}

}  // namespace arli
