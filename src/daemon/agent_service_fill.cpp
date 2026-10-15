#include "agent_service_fill.hpp"

#include "llm_models.hpp"

namespace arli {

void fill_task(const TaskRow& t, v1::TaskInfo* out) {
  out->set_id(t.id);
  out->set_dialog_id(t.dialog_id);
  out->set_turn_id(t.turn_id);
  out->set_status(t.status);
  out->set_worker_id(t.worker_id);
  out->set_error(t.error);
  out->set_created_at(t.created_at);
  out->set_updated_at(t.updated_at);
  out->set_finished_at(t.finished_at);
}

void fill_settings(const SettingsRow& s, v1::Settings* out) {
  out->set_llm_base_url(s.llm_base_url);
  out->set_llm_model(s.llm_model);
  out->set_workdir(s.workdir);
  out->set_has_api_key(!s.llm_api_key.empty());
  out->set_turn_timeout_sec(s.turn_timeout_sec);
  out->set_idle_timeout_sec(s.idle_timeout_sec);
  out->set_cancel_grace_sec(s.cancel_grace_sec);
  out->set_shell_auto(s.shell_auto);
  out->set_shell_ask(s.shell_ask);
  out->set_shell_allow_all(s.shell_allow_all);
  out->set_delete_confirm(s.delete_confirm);
  out->set_list_ignore(s.list_ignore);
  out->set_context_tokens(s.context_tokens);
  out->set_max_steps(s.max_steps);
  out->set_llm_profile(s.llm_profile.empty() ? "gemma4" : s.llm_profile);
  out->set_llm_temperature(s.llm_temperature);
  out->set_shell_timeout_sec(s.shell_timeout_sec);
  out->set_shell_net(s.shell_net);
  out->set_read_only(s.read_only);
  out->set_allow_unsandboxed_shell(s.allow_unsandboxed_shell);
}

void fill_dialog(const DialogRow& d, v1::Dialog* out) {
  out->set_instructions(d.instructions);
  out->set_id(d.id);
  out->set_title(d.title);
  out->set_updated_at(d.updated_at);
}

}  // namespace arli
