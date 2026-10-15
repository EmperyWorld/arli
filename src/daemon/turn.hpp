#pragma once

#include "db.hpp"

#include "arli.grpc.pb.h"

#include <atomic>
#include <functional>
#include <string>

namespace arli {

using EventSink = std::function<bool(const v1::TurnEvent&)>;

class GrpcToolProvider;
struct TurnControl {
  std::atomic<bool>* cancel = nullptr;
  GrpcToolProvider* grpc_tools = nullptr; // Trusted host registry, immutable during a turn.
  std::int64_t task_id = 0; // Set by the trusted Claim handler, never tool arguments.
  std::function<void()> check_workspace;
};

// Per-turn configuration. Comes from ClaimTask (orchestrator owns settings);
// the worker never writes it back to the shared settings table.
struct TurnConfig {
  std::string dialog_instructions;
  std::string llm_base_url;
  std::string llm_model;
  std::string llm_api_key;
  std::string llm_profile = "gemma4";
  double llm_temperature = -1.0;
  std::string workdir;
  // Newline-separated token prefixes. Empty allow-list asks for every command.
  std::string shell_auto;
  std::string shell_ask;
  bool shell_allow_all = false;
  bool delete_confirm = true;
  std::string list_ignore;
  int context_tokens = 16384;
  int max_steps = 256;
  int shell_timeout_sec = 120;
  bool shell_net = false;
  bool read_only = false;
  bool allow_unsandboxed_shell = false;

  static TurnConfig from_settings(const SettingsRow& s) {
    TurnConfig cfg;
    cfg.llm_base_url = s.llm_base_url;
    cfg.llm_model = s.llm_model;
    cfg.llm_api_key = s.llm_api_key;
    cfg.llm_profile = s.llm_profile.empty() ? "gemma4" : s.llm_profile;
    cfg.llm_temperature = s.llm_temperature;
    cfg.workdir = s.workdir;
    cfg.shell_auto = s.shell_auto;
    cfg.shell_ask = s.shell_ask;
    cfg.shell_allow_all = s.shell_allow_all;
    cfg.delete_confirm = s.delete_confirm;
    cfg.list_ignore = s.list_ignore;
    cfg.context_tokens = s.context_tokens > 0 ? s.context_tokens : 16384;
    cfg.max_steps = s.max_steps > 0 ? s.max_steps : 256;
    cfg.shell_timeout_sec = s.shell_timeout_sec > 0 ? s.shell_timeout_sec : 120;
    cfg.shell_net = s.shell_net;
    cfg.read_only = s.read_only;
    cfg.allow_unsandboxed_shell = s.allow_unsandboxed_shell;
    return cfg;
  }
};

// Runs one agent turn; returns assistant message id (0 on hard failure before save).
std::int64_t run_turn(Database& db, const TurnConfig& cfg, std::int64_t dialog_id, const std::string& user_text,
                      std::int64_t turn_id, EventSink sink, TurnControl ctl);

}  // namespace arli
