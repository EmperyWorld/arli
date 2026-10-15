// arli-mock-llm — minimal OpenAI-compatible HTTP/1.1 server for lifecycle tests.
// Scenario is selected by the request's "model":
//   echo          → content "echo: <last user message>"
//   slow:<ms>     → sleep <ms>, then echo
//   hang          → never answers (until the client closes)
//   tool_read     → first round: tool_calls read_file(README.md); then content "read ok"
//   loop          → always tool_calls list_dir(".")
//   fail          → HTTP 500
// GET /v1/models lists the scenarios. GET /stats → {"requests":N}. Test-only, never shipped.
#include <nlohmann/json.hpp>
#include "mock_scenarios.hpp"
#include "mock_recovery_scenarios.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using json = nlohmann::json;

std::atomic<int> g_requests{0};

struct Request {
  std::string method;
  std::string path;
  std::string body;
};

bool read_request(int fd, Request& out) {
  std::string buf;
  char tmp[4096];
  size_t header_end = std::string::npos;
  while (header_end == std::string::npos) {
    const ssize_t n = read(fd, tmp, sizeof(tmp));
    if (n <= 0) {
      return false;
    }
    buf.append(tmp, static_cast<size_t>(n));
    header_end = buf.find("\r\n\r\n");
    if (buf.size() > 1 << 20) {
      return false;
    }
  }
  const std::string head = buf.substr(0, header_end);
  const auto sp1 = head.find(' ');
  const auto sp2 = head.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    return false;
  }
  out.method = head.substr(0, sp1);
  out.path = head.substr(sp1 + 1, sp2 - sp1 - 1);
  size_t content_length = 0;
  std::string lower = head;
  for (auto& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (const auto p = lower.find("content-length:"); p != std::string::npos) {
    content_length = std::strtoul(lower.c_str() + p + 15, nullptr, 10);
  }
  out.body = buf.substr(header_end + 4);
  while (out.body.size() < content_length) {
    const ssize_t n = read(fd, tmp, sizeof(tmp));
    if (n <= 0) {
      return false;
    }
    out.body.append(tmp, static_cast<size_t>(n));
  }
  return true;
}

void respond(int fd, int code, const std::string& body) {
  const char* reason = code == 200 ? "OK" : (code == 404 ? "Not Found" : "Internal Server Error");
  std::string out = "HTTP/1.1 " + std::to_string(code) + " " + reason +
                    "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n" + body;
  size_t off = 0;
  while (off < out.size()) {
    const ssize_t n = write(fd, out.data() + off, out.size() - off);
    if (n <= 0) {
      return;
    }
    off += static_cast<size_t>(n);
  }
}

json completion(const json& message) {
  return json{{"id", "mock"},
              {"object", "chat.completion"},
              {"model", "mock"},
              {"choices", json::array({json{{"index", 0}, {"message", message}, {"finish_reason", "stop"}}})}};
}

json tool_call(const std::string& name, const json& args) {
  return json{{"role", "assistant"},
              {"content", nullptr},
              {"tool_calls", json::array({json{{"id", "call_" + std::to_string(g_requests.load())},
                                               {"type", "function"},
                                               {"function", json{{"name", name}, {"arguments", args.dump()}}}}})}};
}

std::string last_user(const json& messages) {
  if (!messages.is_array()) {
    return "";
  }
  for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
    if ((*it).value("role", "") == "user" && (*it).contains("content") && (*it)["content"].is_string()) {
      return (*it)["content"].get<std::string>();
    }
  }
  return "";
}

bool has_tool_message(const json& messages) {
  if (!messages.is_array()) {
    return false;
  }
  for (const auto& m : messages) {
    if (m.value("role", "") == "tool") {
      return true;
    }
  }
  return false;
}

// Wait until the client hangs up (or a very long time).
void hang_until_closed(int fd) {
  pollfd p{fd, POLLIN | POLLRDHUP, 0};
  poll(&p, 1, 10 * 60 * 1000);
}

void handle_chat(int fd, const Request& req) {
  json body;
  try {
    body = json::parse(req.body);
  } catch (...) {
    respond(fd, 500, R"({"error":"bad json"})");
    return;
  }
  const std::string model = body.value("model", "echo");
  const json messages = body.value("messages", json::array());

  if (!messages.empty() && messages.front().value("content", "").find("ARLI_ACCEPTANCE_PLAN") != std::string::npos) {
    auto criteria = json{{"checks", json::array()}, {"unverifiable", "Mock task has no declared file criteria"}};
    if (model == "acceptance_repair" || model == "acceptance_stuck")
      criteria = {{"checks", json::array({{{"kind", "line_range"}, {"path", "acceptance.txt"},
        {"expected", "{n} DONE"}, {"start_line", 1}, {"end_line", 3}}})}, {"unverifiable", ""}};
    if(model.rfind("procedure_",0)==0){
      criteria={{"checks",json::array()},{"unverifiable",""}};
      if(model!="procedure_only")criteria["checks"].push_back({{"kind","file_equals"},{"path","procedure.txt"},{"expected",model=="procedure_read"?"one\ntwo\nlast\n":"changed\n"}});
      criteria["checks"].push_back({{"kind",model=="procedure_read"?"read_after_each_write":"command_succeeds"},{"path",model=="procedure_read"?"procedure.txt":""},{"expected",model=="procedure_read"?"":"sh ./verify.sh"}});
    }
    respond(fd, 200, completion(mock_scenarios::call("record_acceptance", criteria)).dump());
    return;
  }
  if (auto scenario = mock_recovery::reply(model, messages)) { respond(fd, 200, completion(*scenario).dump()); return; }
  if (auto scenario = mock_scenarios::reply(model, messages)) {
    respond(fd, 200, completion(*scenario).dump());
    return;
  }
  if (model == "fail") {
    respond(fd, 500, R"({"error":{"message":"mock failure"}})");
    return;
  }
  if (model == "hang") {
    hang_until_closed(fd);
    return;
  }
  if (model.rfind("slow:", 0) == 0) {
    const int ms = std::atoi(model.c_str() + 5);
    // Sleep in slices so a closed connection is noticed quickly.
    pollfd p{fd, POLLRDHUP, 0};
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
      if (poll(&p, 1, 100) > 0 && (p.revents & (POLLRDHUP | POLLHUP | POLLERR))) {
        return;
      }
    }
  }
  if (model == "tool_read") {
    if (!has_tool_message(messages)) {
      respond(fd, 200, completion(tool_call("read_file", json{{"path", "README.md"}})).dump());
    } else {
      respond(fd, 200, completion(json{{"role", "assistant"}, {"content", "read ok"}}).dump());
    }
    return;
  }
  if (model == "edit_once" || model == "premature_edit" || model == "append_many") {
    int results = 0;
    int users = 0;
    for (const auto& m : messages) {
      results += m.value("role", "") == "tool";
      users += m.value("role", "") == "user";
    }
    json msg;
    if (model == "premature_edit" && users == 1) {
      msg = {{"role", "assistant"}, {"content", "Сейчас исправлю файл."}};
    } else if (model == "append_many" && results < 8) {
      if (results % 2 == 0) msg = tool_call("read_file", {{"path", "target.txt"}});
      else msg = tool_call("append_file", {{"path", "target.txt"}, {"content", "part" + std::to_string(results / 2) + "\n"}});
    } else if (model != "append_many" && results < 8) {
      // Plenty of legitimate exploration, then just one sufficient edit.
      if (results < 7) {
        msg = tool_call("read_file", {{"path", "target.txt"}, {"start_line", results + 1}, {"end_line", results + 1}});
      } else {
        msg = tool_call("edit_file", {{"path", "target.txt"}, {"start_line", 1}, {"end_line", 1}, {"new_text", "fixed"}});
      }
    } else {
      msg = {{"role", "assistant"}, {"content", "done"}};
    }
    respond(fd, 200, completion(msg).dump());
    return;
  }
  if (model == "loop") {
    respond(fd, 200, completion(tool_call("list_dir", json{{"path", "."}})).dump());
    return;
  }
  respond(fd, 200, completion(json{{"role", "assistant"}, {"content", "echo: " + last_user(messages)}}).dump());
}

void handle(int fd) {
  Request req;
  if (read_request(fd, req)) {
    ++g_requests;
    if (req.method == "GET" && req.path == "/v1/models") {
      if (const char* delay = std::getenv("ARLI_MOCK_MODELS_DELAY_MS"))
        std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(delay)));
      respond(fd, 200,
              json{{"object", "list"},
                   {"data", json::array({json{{"id", "echo"}}, json{{"id", "tool_read"}}, json{{"id", "loop"}}})}}
                  .dump());
    } else if (req.method == "GET" && req.path == "/stats") {
      respond(fd, 200, json{{"requests", g_requests.load()}}.dump());
    } else if (req.method == "POST" && req.path == "/v1/chat/completions") {
      handle_chat(fd, req);
    } else {
      respond(fd, 404, R"({"error":"not found"})");
    }
  }
  close(fd);
}

}  // namespace

int main(int argc, char** argv) {
  const int port = argc >= 2 ? std::atoi(argv[1]) : 18080;
  const int srv = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(srv, 16) != 0) {
    std::perror("arli-mock-llm: bind/listen");
    return 1;
  }
  std::fprintf(stderr, "arli-mock-llm: http://127.0.0.1:%d/v1\n", port);
  for (;;) {
    const int fd = accept(srv, nullptr, nullptr);
    if (fd < 0) {
      continue;
    }
    std::thread(handle, fd).detach();
  }
}
