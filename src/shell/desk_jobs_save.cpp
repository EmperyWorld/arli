#include "facade.hpp"
#include "desk.hpp"

#include <condition_variable>
#include <mutex>

void Desk::runSaveSettings() {
  QString url, model, profile, workdir, key, allow, ask, ignore;
  double temperature = -1.0;
  int turn = 0, idle = 0, grace = 0, context_tokens = 16384, max_steps = 256, shell_timeout = 120;
  bool clear = false, allow_all = false, delete_confirm = true, shell_net = false, read_only = false,
       allow_unsandboxed = false;
  // Read UI fields on the GUI thread before the RPC.
  std::mutex gate;
  std::condition_variable cv;
  bool ready = false;
  onUi([this, &gate, &cv, &ready, &url, &model, &profile, &workdir, &key, &allow, &ask, &ignore, &turn, &idle, &grace,
        &context_tokens, &max_steps, &shell_timeout, &clear, &allow_all, &delete_confirm, &shell_net, &read_only,
        &allow_unsandboxed, &temperature]() {
    url = ui_->llm_base_url_;
    model = ui_->llm_model_;
    profile = ui_->llm_profile_;
    temperature = ui_->llm_temperature_;
    workdir = ui_->workdir_;
    key = ui_->pending_api_key_;
    ui_->pending_api_key_.clear();
    clear = ui_->clear_api_key_;
    ui_->clear_api_key_ = false;
    allow = ui_->shell_auto_;
    ask = ui_->shell_ask_;
    allow_all = ui_->shell_allow_all_;
    delete_confirm = ui_->delete_confirm_;
    ignore = ui_->list_ignore_;
    context_tokens = ui_->context_tokens_;
    max_steps = ui_->max_steps_;
    shell_timeout = ui_->shell_timeout_sec_;
    shell_net = ui_->shell_net_;
    read_only = ui_->read_only_;
    allow_unsandboxed = ui_->allow_unsandboxed_shell_;
    turn = ui_->turn_timeout_;
    idle = ui_->idle_timeout_;
    grace = ui_->cancel_grace_;
    std::lock_guard lock(gate);
    ready = true;
    cv.notify_one();
  });
  {
    std::unique_lock lock(gate);
    cv.wait(lock, [&] { return ready; });
  }
  grpc::ClientContext ctx;
  bearer(ctx);
  arli::v1::Settings req;
  req.set_llm_base_url(url.toStdString());
  req.set_llm_model(model.toStdString());
  req.set_llm_profile(profile.toStdString());
  req.set_llm_temperature(temperature);
  req.set_workdir(workdir.toStdString());
  if (clear) {
    req.set_clear_api_key(true);
  } else if (!key.isEmpty()) {
    req.set_llm_api_key(key.toStdString());
  }
  req.set_turn_timeout_sec(turn);
  req.set_idle_timeout_sec(idle);
  req.set_cancel_grace_sec(grace);
  req.set_shell_auto(allow.toStdString());
  req.set_shell_ask(ask.toStdString());
  req.set_shell_allow_all(allow_all);
  req.set_delete_confirm(delete_confirm);
  req.set_list_ignore(ignore.toStdString());
  req.set_context_tokens(context_tokens);
  req.set_max_steps(max_steps);
  req.set_shell_timeout_sec(shell_timeout);
  req.set_shell_net(shell_net);
  req.set_read_only(read_only);
  req.set_allow_unsandboxed_shell(allow_unsandboxed);
  arli::v1::Settings reply;
  if (!stub()->SetSettings(&ctx, req, &reply).ok()) {
    onUi([this]() {
      if (!ui_->busy_) {
        ui_->settings_state_ = QStringLiteral("не записалось");
        ui_->status_text_ = ui_->settings_state_;
        emit ui_->stateChanged();
      }
    });
    return;
  }
  onUi([this, reply]() {
    ui_->has_api_key_ = reply.has_api_key();
    if (!reply.llm_profile().empty()) {
      ui_->llm_profile_ = QString::fromStdString(reply.llm_profile());
    }
    ui_->llm_temperature_ = reply.has_llm_temperature() ? reply.llm_temperature() : -1.0;
    ui_->shell_allow_all_ = reply.shell_allow_all();
    ui_->delete_confirm_ = reply.delete_confirm();
    if (reply.context_tokens() > 0) {
      ui_->context_tokens_ = reply.context_tokens();
      ui_->context_window_ = reply.context_tokens();
    }
    if (reply.max_steps() > 0) {
      ui_->max_steps_ = reply.max_steps();
    }
    if (reply.shell_timeout_sec() > 0) {
      ui_->shell_timeout_sec_ = reply.shell_timeout_sec();
    }
    ui_->shell_net_ = reply.shell_net();
    ui_->read_only_ = reply.read_only();
    ui_->allow_unsandboxed_shell_ = reply.allow_unsandboxed_shell();
    ui_->settings_dirty_ = false;
    ui_->settings_state_ = reply.shell_allow_all()
                               ? QStringLiteral("записано: команды без вопроса")
                               : QStringLiteral("записано: команды спрашивают");
    ui_->settings_state_ += reply.delete_confirm() ? QStringLiteral(", удаление с разрешения")
                                                    : QStringLiteral(", удаление без вопроса");
    ui_->settings_state_ += reply.llm_model_listed() ? QStringLiteral(", модель в /models")
                                                      : QStringLiteral(", модель не в /models");
    ui_->llm_reachable_ = reply.llm_model_listed();
    if (!ui_->busy_) {
      ui_->status_text_ = ui_->settings_state_;
    }
    emit ui_->settingsChanged();
    emit ui_->contextChanged();
    emit ui_->stateChanged();
  });
}
