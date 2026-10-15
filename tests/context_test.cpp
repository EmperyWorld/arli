#include "context.hpp"
#include "context_summary.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

static int fail(const std::string& msg) {
  std::cerr << "FAIL " << msg << "\n";
  return 1;
}

int main() {
  {
    const std::string source(20000, 'x');
    if (arli::context::tool_output_for_model(source, arli::context::source_output_limit(49152)) != source)
      return fail("large window should preserve source reads instead of hiding the middle");
    if (arli::context::source_output_limit(1000000) != 48000 ||
        arli::context::tool_output_for_model(source, arli::context::source_output_limit(8192)).size() > 8192)
      return fail("source output limit must remain bounded");
  }
  {
    // A large first observation has only three messages: compact alone cannot fold it.
    using json = nlohmann::json;
    json messages = json::array({json{{"role", "system"}, {"content", "keep API"}},
      json{{"role", "assistant"}, {"tool_calls", json::array({json{{"id", "read"},
        {"function", {{"name", "read_file"}, {"arguments", "{}"}}}}})}},
      json{{"role", "tool"}, {"tool_call_id", "read"}, {"content", "OK\n" + std::string(30000, 'x')}}});
    arli::context::fit_budget(messages, 4096);
    if (arli::context::estimate_tokens(messages) > 4096 * 4 / 5 || messages.size() != 3 ||
        messages[2]["tool_call_id"] != "read" || messages[0]["content"] != "keep API")
      return fail("first large observation did not fit or lost its call");
  }
  {
    const std::string s(8000, 'a');
    const auto clipped = arli::context::tool_output_for_model(s, 200);
    if (clipped.size() >= 8000) {
      return fail("tool clip should shrink");
    }
    if (clipped.find("середина обрезана") == std::string::npos) {
      return fail("head/tail marker missing");
    }
    if (clipped.front() != 'a' || clipped.back() != 'a') {
      return fail("head/tail bytes missing");
    }
  }

  {
    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "system"}, {"content", "sys"}});
    nlohmann::json tc = nlohmann::json::object();
    tc["id"] = "1";
    tc["type"] = "function";
    tc["function"] = {{"name", "edit_file"}, {"arguments", "{\"path\":\"a.html\"}"}};
    messages.push_back({{"role", "assistant"}, {"tool_calls", nlohmann::json::array({tc})}});
    messages.push_back({{"role", "tool"}, {"tool_call_id", "1"}, {"content", "OK\n" + std::string(9000, 'x')}});
    const std::string sum = arli::context::build_fold_summary(messages, 1, 3);
    if (sum.find("a.html") == std::string::npos) {
      return fail("summary missing changed path");
    }
    arli::context::fit_budget(messages, 4096);
    if (arli::context::estimate_tokens(messages) > 4096) {
      return fail("fit_budget still over window");
    }
  }

  {
    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "system"}, {"content", "s"}});
    for (int i = 0; i < 30; ++i) {
      messages.push_back({{"role", "user"}, {"content", std::string(400, 'u')}});
      messages.push_back({{"role", "assistant"}, {"content", std::string(400, 'a')}});
    }
    arli::context::fit_budget(messages, 2048);
    if (messages.size() >= 62) {
      return fail("expected fold to shrink message count");
    }
  }

  {
    using json = nlohmann::json;
    json messages = json::array({json{{"role", "system"}, {"content", "task: keep PUBLIC_API unchanged"}},
      json{{"role", "user"}, {"content", "Do not change PUBLIC_API; fix calculation"}}});
    auto call = [&](const char* id, const char* path, const char* result) {
      messages.push_back({{"role", "assistant"}, {"tool_calls", json::array({json{{"id", id},
        {"function", {{"name", "edit_file"}, {"arguments", json{{"path", path}}.dump()}}}}})}});
      messages.push_back({{"role", "tool"}, {"tool_call_id", id}, {"content", result}});
    };
    call("ok", "success.cpp", "OK\nedited");
    call("fail", "failed.cpp", "ERROR\npath not found");
    const std::string summary = arli::context::build_fold_summary(messages, 1, messages.size());
    const auto data = json::parse(summary.substr(summary.find('\n') + 1));
    if (data["Изменены"] != json::array({"success.cpp"})) return fail("failed write called successful");
    if (data["Ошибки"].empty() || summary.find("PUBLIC_API") == std::string::npos)
      return fail("summary lost constraints or error");
    json next = json::array({json{{"role", "user"}, {"content", summary}}});
    const auto repeated = arli::context::build_fold_summary(next, 0, 1);
    if (repeated.find("PUBLIC_API") == std::string::npos || repeated.find("success.cpp") == std::string::npos)
      return fail("repeated folding lost state");
    for (int i = 0; i < 30; ++i) {
      messages.push_back({{"role", "assistant"}, {"content", std::string(400, 'a')}});
      messages.push_back({{"role", "user"}, {"content", "progress"}});
    }
    arli::context::fit_budget(messages, 4096);
    if (messages.front()["content"] != "task: keep PUBLIC_API unchanged") return fail("pinned task lost");
  }

  std::cout << "OK context-budget\n";
  return 0;
}
