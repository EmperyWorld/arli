#pragma once

#include <string>
#include <string_view>

namespace arli {

// Resolve arli-agent binary (ARLI_AGENT_BIN or dirname(/proc/self/exe)/arli-agent).
std::string agent_binary_path();

// Token handed to the worker via env ARLI_INTERNAL_TOKEN (never argv). Set once by serve().
void set_worker_internal_token(std::string token);

// Fork+exec arli-agent pointed at listen endpoint with an allowlisted environment.
// Returns child pid or 0 on failure. Exactly one worker process at a time.
int spawn_agent_worker(const std::string& listen_endpoint);

// Validate the ephemeral launch secret; return the expected child PID, or 0.
int worker_session_pid(std::string_view token);

// Current worker pid (0 if none spawned / already reaped).
int agent_pid();
// kill(pid, 0) liveness.
bool agent_alive();
// waitpid(WNOHANG); true if the worker exited (status logged) and the pid was cleared.
bool reap_agent();
// Send a signal to the worker (no-op if none).
void signal_agent(int sig);
// SIGTERM, wait up to `term_wait_ms`, then SIGKILL; always reaps. Returns true if a process was killed.
bool kill_agent(int term_wait_ms);

}  // namespace arli
