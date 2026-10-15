#include "turn_policy.hpp"
#include "tools.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

static int fail(const char* msg) {
  std::fprintf(stderr, "FAIL %s\n", msg);
  return 1;
}

int main() {
  if (!arli::wants_file_work("Продолжаем улучшать")) return fail("wants improve");
  if (!arli::wants_file_work("да, делай")) return fail("wants delay");
  if (!arli::wants_file_work("вноси изменения")) return fail("wants vnosi");
  if (arli::wants_file_work("привет, как дела?")) return fail("false positive greeting");

  if (!arli::is_mutation_tool("edit_file")) return fail("edit");
  if (!arli::is_mutation_tool("create_file")) return fail("create");
  if (!arli::is_mutation_tool("append_file")) return fail("append");
  if (arli::is_mutation_tool("read_file")) return fail("read not mutation");

  const std::string schema = arli::tools_openai_schema_json();
  if (schema.find("\"create_file\"") == std::string::npos) return fail("schema create_file");
  if (schema.find("\"edit_file\"") == std::string::npos) return fail("schema edit_file");
  if (schema.find("\"append_file\"") == std::string::npos) return fail("schema append_file");
  // write_file must not be offered to the model
  if (schema.find("\"write_file\"") != std::string::npos) return fail("schema still has write_file");

  // Gemma often uses filePath / omits braces — coerce + repair.
  {
    nlohmann::json a;
    std::string err;
    if (!arli::parse_tool_args(R"({"filePath":"index.html","old_text":"a","new_text":"b")", a, err))
      return fail(("repair parse: " + err).c_str());
    if (arli::coerce_path_arg(a) != "index.html") return fail("coerce filePath");
    auto r = arli::dispatch_tool(std::filesystem::path{"/tmp"}, "edit_file",
                                 R"({"file":"nope.html","old_text":"x","new_text":"y"})");
    if (r.ok) return fail("missing file should fail");
    r = arli::dispatch_tool(std::filesystem::path{"/tmp"}, "edit_file",
                            R"({"old_text":"x","new_text":"y"})");
    if (r.ok || r.output.find("path отсутствует") == std::string::npos)
      return fail(("expected path missing msg: " + r.output).c_str());
    std::printf("OK path coerce/repair\n");
  }

  // Autofill path from recent reads (Gemma drops path).
  {
    std::vector<std::pair<std::string, std::string>> reads = {
        {"core.js", "export class Core {}\n"},
        {"index.html", "<h1 id=\"title\">Before</h1>\n"},
    };
    nlohmann::json a = {{"old_text", "<h1 id=\"title\">Before</h1>"}, {"new_text", "<h1>X</h1>"}};
    const std::string inferred = arli::infer_path_from_reads(a, reads);
    if (inferred != "index.html") return fail(("infer got: " + inferred).c_str());
    const std::string filled = arli::args_with_path(R"({"old_text":"a","new_text":"b"})", "index.html");
    if (filled.find("\"path\":\"index.html\"") == std::string::npos) return fail("args_with_path");
    std::printf("OK path autofill infer\n");
  }

  // Different replacements and append chunks must not trigger loop detection.
  const std::string first = R"({"path":"a.cpp","start_line":1,"end_line":1,"new_text":"one"})";
  const std::string second = R"({"path":"a.cpp","start_line":1,"end_line":1,"new_text":"two"})";
  if (arli::tool_call_key("edit_file", first) == arli::tool_call_key("edit_file", second))
    return fail("different edits share loop key");
  if (arli::tool_call_key("append_file", R"({"path":"a","content":"one"})") ==
      arli::tool_call_key("append_file", R"({"path":"a","content":"two"})"))
    return fail("different appends share loop key");
  if (arli::tool_call_key("read_file", R"({"path":"a","start_line":1})") !=
      arli::tool_call_key("read_file", R"({"start_line":1,"path":"a"})"))
    return fail("JSON key order changes loop key");

  for (const char* request : {"напиши объяснение", "напиши стих", "как исправить ошибку в main.cpp?",
                              "покажи изменения", "prefix suffix", "не редактируй файлы, только анализ"})
    if (arli::wants_file_work(request)) return fail(request);
  for (const char* request : {"переименуй файл a.txt в b.txt", "отредактируй main.cpp", "замени строку в a.txt",
                              "удали устаревший код", "создай файл x.py", "объясни проблему и исправь main.cpp"})
    if (!arli::wants_file_work(request)) return fail(request);
  if (!arli::classify_turn_intent("НЕ РЕДАКТИРУЙ файлы").forbid_mutation) return fail("explicit read-only");
  if (arli::classify_turn_intent("Новые файлы нужны").forbid_create) return fail("new file permission");
  if (!arli::classify_turn_intent("Не создавай новые файлы").forbid_create) return fail("no new files");
  {
    const std::vector<std::pair<std::string, std::string>> reads = {
      {"a.cpp", "# a.cpp lines 1-2\n1|alpha\n2|beta\n"}, {"b.cpp", "other\n"}};
    if (arli::infer_path_from_reads({{"old_text", "alpha\nbeta"}}, reads) != "a.cpp")
      return fail("numbered read path inference");
    if (!arli::infer_path_from_reads({{"old_text", "missing"}}, reads).empty())
      return fail("guessed last read file");
    if (!arli::infer_path_from_reads({{"start_line", 1}, {"new_text", "x"}}, reads).empty())
      return fail("ambiguous line edit path");
  }

  {
    nlohmann::json args;
    std::string error;
    if (!arli::parse_tool_args(R"({"new_text":"literal ,} and ,]","path":"a", })", args, error) ||
        args["new_text"] != "literal ,} and ,]") return fail("JSON repair changed file content");
    if (!arli::parse_tool_args(R"({"paths":[{"path":"a")", args, error) || !args["paths"].is_array())
      return fail("nested closing order repair");
    if (arli::parse_tool_args("{\"new_text\":\"truncated", args, error))
      return fail("incomplete replacement string accepted");
  }

  std::printf("MUTATION_GATE_OK\n");
  return 0;
}
