#pragma once
#include "db.hpp"
#include <nlohmann/json.hpp>

namespace arli {
// Deliberately excludes llm_api_key. Credentials stay in the settings store and
// are read at dispatch only if the endpoint still matches the queued task.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SettingsRow,
  llm_base_url, llm_model, llm_profile, llm_temperature, workdir, turn_timeout_sec, idle_timeout_sec,
  cancel_grace_sec, shell_auto, shell_ask, shell_allow_all, delete_confirm,
  list_ignore, context_tokens, max_steps, shell_timeout_sec, shell_net, read_only,
  allow_unsandboxed_shell)
}
