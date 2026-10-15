#include "job_maintenance.hpp"
#include "redact.hpp"
#include "server.hpp"

#include "agent_service.hpp"
#include "auth.hpp"
#include "db.hpp"
#include "hub.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "secrets.hpp"
#include "spawn_agent.hpp"
#include "watchdog.hpp"

#include "arli.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include <signal.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

namespace arli {
namespace {

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;

constexpr int kShutdownDeadlineMs = 5000;

// gRPC `arli.v1.AgentHub` — the single worker Session (bi-di stream).
class AgentHubService final : public v1::AgentHub::Service {
 public:
  Status Session(ServerContext* ctx, ServerReaderWriter<v1::OrchToWorker, v1::WorkerToOrch>* stream) override {
    const auto& metadata = ctx->client_metadata();
    const auto launch = metadata.find(auth::kWorkerLaunchHeader);
    if (metadata.count(auth::kWorkerLaunchHeader) != 1)
      return Status(grpc::StatusCode::UNAUTHENTICATED, "worker launch identity required");
    const std::string launch_token(launch->second.data(), launch->second.size());
    const int expected_pid = worker_session_pid(launch_token);
    if (!expected_pid) return Status(grpc::StatusCode::UNAUTHENTICATED, "invalid worker launch identity");
    auto& hub = OrchestratorHub::instance();
    const auto generation = hub.try_attach_session();
    if (!generation) {
      return Status(grpc::StatusCode::ALREADY_EXISTS, "worker Session already open");
    }

    std::atomic<bool> live{true};
    std::thread writer([&] {
      while (live.load() && !ctx->IsCancelled()) {
        if (!hub.session_active(generation)) { ctx->TryCancel(); break; }
        auto msg = hub.pop_outbound(generation, 200);
        if (!msg) {
          continue;
        }
        if (!stream->Write(*msg)) {
          ctx->TryCancel();
          break;
        }
      }
    });

    bool invalid = false;
    v1::WorkerToOrch in;
    while (stream->Read(&in)) {
      if ((in.has_hello() && (in.hello().pid() != expected_pid || worker_session_pid(launch_token) != expected_pid)) ||
          !hub.on_worker_message(generation, in)) {
        invalid = true;
        ctx->TryCancel();
        break;
      }
    }

    live.store(false);
    hub.wake_outbound();
    writer.join();
    hub.on_session_end(generation);
    return invalid ? Status(grpc::StatusCode::INVALID_ARGUMENT, "invalid worker session frame") : Status::OK;
  }
};

// SIGINT/SIGTERM are blocked in every thread; the main thread waits for them synchronously.
int wait_for_signal(const sigset_t& set) {
  int sig = 0;
  sigwait(&set, &sig);
  return sig;
}

}  // namespace

int serve(const std::string& endpoint, const std::string& db_file) {
  sigset_t sigs;
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGINT);
  sigaddset(&sigs, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &sigs, nullptr);
  signal(SIGPIPE, SIG_IGN);

  // Lock before opening/reconciling the database: a second daemon must not mutate it.
  const auto lock_path = std::filesystem::weakly_canonical(db_file).string() + ".daemon.lock";
  std::filesystem::create_directories(std::filesystem::path(lock_path).parent_path());
  struct LockFile { int fd; ~LockFile() { if (fd >= 0) close(fd); } };
  LockFile owner{open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600)};
  if (owner.fd < 0 || flock(owner.fd, LOCK_EX | LOCK_NB) != 0) {
    std::fprintf(stderr, "arli-daemon: database already owned or lock unavailable\n");
    return 1;
  }
  redact::enable_persistence(state_root()/"redaction.fingerprints");
  auto db = std::make_shared<Database>(db_file);
  db->init();
  // A claimed execution cannot be replayed safely after a crash: its tools may
  // already have external effects. Unclaimed queued work is durable and resumes.
  if (const int n = db->fail_running_tasks("reconcile_startup"); n > 0) {
    log::warn("orch", "reconcile_startup: stale running tasks → failed " + log::kv("count", n));
  }

  // Zero-trust loopback: both tokens are owned by the daemon, 0600 in state dir.
  const std::string ui_token = secrets::load_or_create(secrets::ui_token_path());
  const std::string internal_token = secrets::load_or_create(secrets::internal_token_path());
  if (ui_token.empty() || internal_token.empty()) {
    std::fprintf(stderr, "arli-daemon: cannot create tokens in %s\n", state_root().c_str());
    return 3;
  }
  set_worker_internal_token(internal_token);

  auto& hub = OrchestratorHub::instance();
  hub.init(db);

  AgentService ui(db);
  AgentHubService hub_service;

  ServerBuilder builder;
  // Default SO_REUSEPORT lets a second daemon share the port. The shell then
  // talks to a random one and authenticated calls fail without a visible error.
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.SetMaxReceiveMessageSize(4*1024*1024);
  builder.SetMaxSendMessageSize(4*1024*1024);
  builder.AddListeningPort(endpoint, auth::server_credentials(ui_token, internal_token));
  builder.RegisterService(&ui);
  builder.RegisterService(&hub_service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (!server) {
    std::fprintf(stderr, "arli-daemon: cannot listen on %s\n", endpoint.c_str());
    return 1;
  }

  JobMaintenance jobs(db);
  Watchdog watchdog(endpoint);
  watchdog.start();

  log::info("orch", "serving " + log::kv("endpoint", endpoint) + " " + log::kv("version", kVersion) + " " +
                        log::kv("slice", kSlice));
  const int sig = wait_for_signal(sigs);
  log::info("orch", "graceful shutdown " + log::kv("signal", sig));
  hub.request_shutdown();
  server->Shutdown(std::chrono::system_clock::now() + std::chrono::milliseconds(kShutdownDeadlineMs));
  server->Wait();
  watchdog.stop();
  return 0;
}

}  // namespace arli
