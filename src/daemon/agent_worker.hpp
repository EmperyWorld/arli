#pragma once
// arli-agent worker internals: Session write path, cancel/shutdown flags, heartbeat, run_claim.
// Shared by agent_main.cpp (Session loop) and agent_worker.cpp.

#include "db.hpp"

#include "arli.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <functional>
#include <cstdint>
#include <memory>
#include <string>

namespace arli::worker {

using Stream = grpc::ClientReaderWriter<v1::WorkerToOrch, v1::OrchToWorker>;

struct Flags {
  std::atomic<bool> cancel{false};    // cancel the current turn
  std::atomic<bool> shutdown{false};  // leave after the current turn
  std::atomic<bool> exit{false};      // main is returning; stops the heartbeat thread
  std::atomic<bool> busy{false};
  std::atomic<std::int64_t> task{0};
  std::atomic<std::int64_t> dialog{0};
  std::atomic<std::int64_t> turn{0};
};

Flags& flags();

// Session stream / context handles (thread-safe setters).
void set_stream(std::shared_ptr<Stream> s);
void set_session_ctx(grpc::ClientContext* ctx);
bool write_msg(v1::WorkerToOrch msg);
// Apply interaction and publish ACK before its newly unblocked turn can emit events.
bool apply_and_ack(std::int64_t id, const std::function<bool()>& apply);
// TryCancel() on the live Session context: breaks the main thread out of Read().
void cancel_session();

// Heartbeat every 15 s while busy; also the thread that leaves Read() on SIGTERM while idle.
void heartbeat_loop();

// Run one ClaimTask to completion (Done / TaskFailed). Clears busy at the end.
void run_claim(v1::ClaimTask claim, std::shared_ptr<Database> db);
void reject_claim(const v1::ClaimTask& claim, const std::string& reason);

}  // namespace arli::worker
