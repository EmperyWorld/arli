#include "db.hpp"
#include "redact.hpp"
#include "db_impl.hpp"
#include "paths.hpp"
#include "llm_sampling.hpp"
#include <stdexcept>

namespace arli {

using namespace dbdetail;

namespace {

std::string default_workdir() {
  return (state_root() / "workspace").string();
}

}  // namespace

SettingsRow Database::get_settings() {
  DbLock lock(impl_->mu);
  SettingsRow out;
  Stmt st(impl_->db, "SELECT key, value FROM settings", "prep settings");
  auto to_int = [](const std::string& v, int fallback) {
    try {
      return v.empty() ? fallback : std::stoi(v);
    } catch (...) {
      return fallback;
    }
  };
  while (st.row()) {
    const std::string key = col_text(st.get(), 0);
    const std::string val = col_text(st.get(), 1);
    if (key == "llm_base_url") {
      out.llm_base_url = val;
    } else if (key == "llm_model") {
      out.llm_model = val;
    } else if (key == "llm_profile") {
      out.llm_profile = val;
    } else if (key == "llm_temperature") {
      try {
        std::size_t end = 0;
        const double value = std::stod(val, &end);
        if (end == val.size() && valid_llm_temperature(value)) out.llm_temperature = value;
      } catch (...) {} // Invalid persisted values retain the historical profile default.
    } else if (key == "llm_api_key") {
      out.llm_api_key = val;
      redact::remember(val);
    } else if (key == "workdir") {
      out.workdir = val;
    } else if (key == "turn_timeout_sec") {
      out.turn_timeout_sec = to_int(val, out.turn_timeout_sec);
    } else if (key == "idle_timeout_sec") {
      out.idle_timeout_sec = to_int(val, out.idle_timeout_sec);
    } else if (key == "cancel_grace_sec") {
      out.cancel_grace_sec = to_int(val, out.cancel_grace_sec);
    } else if (key == "shell_auto") {
      out.shell_auto = val;
    } else if (key == "shell_ask") {
      out.shell_ask = val;
    } else if (key == "shell_allow_all") {
      out.shell_allow_all = val == "1" || val == "true";
    } else if (key == "delete_confirm") {
      out.delete_confirm = val != "0" && val != "false";
    } else if (key == "list_ignore") {
      out.list_ignore = val;
    } else if (key == "context_tokens") {
      out.context_tokens = to_int(val, out.context_tokens);
    } else if (key == "max_steps") {
      out.max_steps = to_int(val, out.max_steps);
    } else if (key == "shell_timeout_sec") {
      out.shell_timeout_sec = to_int(val, out.shell_timeout_sec);
    } else if (key == "shell_net") {
      out.shell_net = val == "1" || val == "true";
    } else if (key == "read_only") {
      out.read_only = val == "1" || val == "true";
    } else if (key == "allow_unsandboxed_shell") {
      out.allow_unsandboxed_shell = val == "1" || val == "true";
    }
  }
  if (out.workdir.empty()) {
    out.workdir = default_workdir();
  }
  return out;
}

SettingsRow Database::set_settings(const SettingsRow& in) {
  if (!valid_llm_temperature(in.llm_temperature)) throw std::invalid_argument("invalid llm_temperature");
  redact::remember(in.llm_api_key);
  DbLock lock(impl_->mu);
  auto put = [&](const char* key, const std::string& value) {
    Stmt st(impl_->db,
            "INSERT INTO settings(key,value) VALUES(?1,?2) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
            "prep upsert");
    st.bind(1, std::string(key));
    st.bind(2, value);
    st.exec("upsert");
  };
  const SettingsRow defaults;
  put("llm_base_url", in.llm_base_url);
  put("llm_model", in.llm_model);
  put("llm_profile", in.llm_profile.empty() ? defaults.llm_profile : in.llm_profile);
  put("llm_temperature", std::to_string(in.llm_temperature));
  put("llm_api_key", in.llm_api_key);
  put("workdir", in.workdir.empty() ? default_workdir() : in.workdir);
  put("turn_timeout_sec", std::to_string(in.turn_timeout_sec > 0 ? in.turn_timeout_sec : defaults.turn_timeout_sec));
  put("idle_timeout_sec", std::to_string(in.idle_timeout_sec > 0 ? in.idle_timeout_sec : defaults.idle_timeout_sec));
  put("cancel_grace_sec", std::to_string(in.cancel_grace_sec > 0 ? in.cancel_grace_sec : defaults.cancel_grace_sec));
  put("shell_auto", in.shell_auto);
  put("shell_ask", in.shell_ask);
  put("shell_allow_all", in.shell_allow_all ? "1" : "0");
  put("delete_confirm", in.delete_confirm ? "1" : "0");
  put("list_ignore", in.list_ignore);
  put("context_tokens", std::to_string(in.context_tokens > 0 ? in.context_tokens : defaults.context_tokens));
  put("max_steps", std::to_string(in.max_steps > 0 ? in.max_steps : defaults.max_steps));
  put("shell_timeout_sec",
      std::to_string(in.shell_timeout_sec > 0 ? in.shell_timeout_sec : defaults.shell_timeout_sec));
  put("shell_net", in.shell_net ? "1" : "0");
  put("read_only", in.read_only ? "1" : "0");
  put("allow_unsandboxed_shell", in.allow_unsandboxed_shell ? "1" : "0");
  return get_settings();
}

}  // namespace arli
