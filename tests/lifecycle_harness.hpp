#pragma once
// Test-only process/gRPC helpers for arli-test-lifecycle.

#include "auth.hpp"
#include "secrets.hpp"

#include "arli.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lt {

inline int g_failed = 0;

#define LT_CHECK(cond, msg)                                                   \
  do {                                                                        \
    if (cond) {                                                               \
      std::fprintf(stderr, "  ok   %s\n", msg);                               \
    } else {                                                                  \
      std::fprintf(stderr, "  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); \
      ++lt::g_failed;                                                         \
    }                                                                         \
  } while (0)

inline void sleep_ms(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline int free_port() {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  socklen_t len = sizeof(addr);
  getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
  const int port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

inline bool pid_alive(int pid) {
  return pid > 0 && kill(pid, 0) == 0;
}

// Child process with explicit env additions (inherits the rest).
struct Proc {
  pid_t pid = 0;

  bool start(const std::string& bin, const std::vector<std::string>& args,
             const std::vector<std::pair<std::string, std::string>>& env) {
    pid = fork();
    if (pid < 0) {
      return false;
    }
    if (pid == 0) {
      for (const auto& [k, v] : env) {
        setenv(k.c_str(), v.c_str(), 1);
      }
      std::vector<char*> argv;
      argv.push_back(const_cast<char*>(bin.c_str()));
      for (const auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
      }
      argv.push_back(nullptr);
      execv(bin.c_str(), argv.data());
      _exit(127);
    }
    return true;
  }
  // Returns exit status, or -1 if still running after timeout.
  int wait(int timeout_ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < until) {
      int st = 0;
      if (waitpid(pid, &st, WNOHANG) == pid) {
        pid = 0;
        return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
      }
      sleep_ms(50);
    }
    return -1;
  }
  void kill9() {
    if (pid > 0) {
      kill(pid, SIGKILL);
      wait(2000);
    }
  }
  void term() {
    if (pid > 0) {
      kill(pid, SIGTERM);
    }
  }
};

struct Collected {
  std::vector<arli::v1::TurnEvent> events;
  grpc::Status status;
  bool has(const std::function<bool(const arli::v1::TurnEvent&)>& pred) const {
    for (const auto& e : events) {
      if (pred(e)) {
        return true;
      }
    }
    return false;
  }
  bool done() const {
    return has([](const auto& e) { return e.has_done(); });
  }
  bool error_contains(const std::string& s) const {
    return has([&](const auto& e) { return e.has_error() && e.error().message().find(s) != std::string::npos; });
  }
  std::string done_text() const {
    for (const auto& e : events) {
      if (e.has_done()) {
        return e.done().assistant_text();
      }
    }
    return "";
  }
};

struct Client {
  std::unique_ptr<arli::v1::Agent::Stub> stub;
  std::string token;

  explicit Client(const std::string& endpoint) : stub(arli::v1::Agent::NewStub(arli::auth::make_channel(endpoint))) {}

  // Fresh channel (after a daemon restart the old one sits in reconnect backoff).
  void reconnect(const std::string& endpoint) { stub = arli::v1::Agent::NewStub(arli::auth::make_channel(endpoint)); }

  void auth(grpc::ClientContext& ctx) const { arli::auth::bearer(ctx, token); }

  bool health(arli::v1::HealthReply* out = nullptr) {
    grpc::ClientContext ctx;
    ctx.set_wait_for_ready(true);
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(700));
    arli::v1::HealthReply rep;
    const auto st = stub->Health(&ctx, arli::v1::HealthRequest(), &rep);
    if (out) {
      *out = rep;
    }
    return st.ok() && rep.serving();
  }
  bool wait_health(int timeout_ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < until) {
      if (health()) {
        return true;
      }
      sleep_ms(100);
    }
    return false;
  }
  bool wait_worker(const std::string& state, int timeout_ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < until) {
      arli::v1::HealthReply h;
      if (health(&h) && h.worker_state() == state) {
        return true;
      }
      sleep_ms(100);
    }
    return false;
  }
  std::int64_t create_dialog(const std::string& title) {
    grpc::ClientContext ctx;
    auth(ctx);
    arli::v1::CreateDialogRequest req;
    req.set_title(title);
    arli::v1::Dialog d;
    stub->CreateDialog(&ctx, req, &d);
    return d.id();
  }
  bool set_model(const std::string& base_url, const std::string& model, const std::string& workdir, int idle_sec,
                 int grace_sec) {
    grpc::ClientContext ctx;
    auth(ctx);
    arli::v1::Settings s;
    s.set_llm_base_url(base_url);
    s.set_llm_model(model);
    s.set_workdir(workdir);
    s.set_idle_timeout_sec(idle_sec);
    s.set_cancel_grace_sec(grace_sec);
    arli::v1::Settings out;
    return stub->SetSettings(&ctx, s, &out).ok();
  }
  Collected send(std::int64_t dialog_id, const std::string& text, int deadline_ms = 30000) {
    Collected c;
    grpc::ClientContext ctx;
    auth(ctx);
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_ms));
    arli::v1::SendRequest req;
    req.set_dialog_id(dialog_id);
    req.set_text(text);
    auto reader = stub->Send(&ctx, req);
    arli::v1::TurnEvent ev;
    while (reader->Read(&ev)) {
      c.events.push_back(ev);
    }
    c.status = reader->Finish();
    return c;
  }
  bool cancel(std::int64_t dialog_id) {
    grpc::ClientContext ctx;
    auth(ctx);
    arli::v1::CancelRequest req;
    req.set_dialog_id(dialog_id);
    arli::v1::CancelReply rep;
    return stub->Cancel(&ctx, req, &rep).ok() && rep.ok();
  }
  bool hard_stop(std::int64_t dialog_id) {
    grpc::ClientContext ctx;
    auth(ctx);
    arli::v1::HardStopRequest req;
    req.set_dialog_id(dialog_id);
    arli::v1::HardStopReply rep;
    return stub->HardStop(&ctx, req, &rep).ok() && rep.ok();
  }
  std::vector<arli::v1::TaskInfo> tasks(std::int64_t dialog_id) {
    grpc::ClientContext ctx;
    auth(ctx);
    arli::v1::ListTasksRequest req;
    req.set_dialog_id(dialog_id);
    arli::v1::ListTasksReply rep;
    stub->ListTasks(&ctx, req, &rep);
    return {rep.tasks().begin(), rep.tasks().end()};
  }
  // Newest task of the dialog.
  arli::v1::TaskInfo last_task(std::int64_t dialog_id) {
    auto v = tasks(dialog_id);
    return v.empty() ? arli::v1::TaskInfo() : v.front();
  }
};

}  // namespace lt
