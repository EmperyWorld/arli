#include "facade.hpp"
#include "desk.hpp"

#include <chrono>
#include <vector>

namespace {

QString fromBytes(const std::string& s) {
  return QString::fromStdString(s);
}

}  // namespace

void Desk::runHealth() {
  grpc::ClientContext ctx;
  bearer(ctx);
  ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(700));
  arli::v1::HealthReply reply;
  const auto st = stub()->Health(&ctx, arli::v1::HealthRequest(), &reply);
  const bool up = st.ok() && reply.serving();
  onUi([this, up, reply, err = fromBytes(st.error_message())]() {
    ui_->daemon_up_ = up;
    ui_->slice_text_ = fromBytes(reply.slice());
    ui_->version_text_ = fromBytes(reply.version());
    ui_->worker_state_ = up ? fromBytes(reply.worker_state()) : QStringLiteral("none");
    ui_->worker_pid_ = up ? reply.worker_pid() : 0;
    ui_->queue_depth_ = up ? reply.queue_depth() : 0;
    ui_->llm_reachable_ = up && reply.llm_reachable();
    if (!ui_->busy_) {
      ui_->status_text_ = up ? (QStringLiteral("демон ") + ui_->slice_text_ + QStringLiteral(" · ") +
                                ui_->worker_state_)
                             : (QStringLiteral("нет демона: ") + err);
      if (up && ui_->queue_depth_ > 0) {
        ui_->status_text_ += QStringLiteral(" · очередь ") + QString::number(ui_->queue_depth_);
      }
    }
    emit ui_->stateChanged();
  });
  runJobs();
}

void Desk::runRefresh() {
  runHealth();
  onUi([this]{ui_->dialogs_->refreshPages();});
  grpc::ClientContext sctx;
  bearer(sctx);
  arli::v1::Settings settings;
  const bool got = stub()->GetSettings(&sctx, arli::v1::GetSettingsRequest(), &settings).ok();
  onUi([this, got, settings]() mutable {
    if (got) {
      ui_->llm_base_url_ = fromBytes(settings.llm_base_url());
      ui_->llm_model_ = fromBytes(settings.llm_model());
      ui_->llm_profile_ = fromBytes(settings.llm_profile());
      if (ui_->llm_profile_.isEmpty()) {
        ui_->llm_profile_ = QStringLiteral("gemma4");
      }
      ui_->workdir_ = fromBytes(settings.workdir());
      ui_->has_api_key_ = settings.has_api_key();
      if (settings.turn_timeout_sec() > 0) {
        ui_->turn_timeout_ = settings.turn_timeout_sec();
      }
      if (settings.idle_timeout_sec() > 0) {
        ui_->idle_timeout_ = settings.idle_timeout_sec();
      }
      if (settings.cancel_grace_sec() > 0) {
        ui_->cancel_grace_ = settings.cancel_grace_sec();
      }
      if (!ui_->settings_dirty_) {
        ui_->llm_temperature_ = settings.has_llm_temperature() ? settings.llm_temperature() : -1.0;
        ui_->shell_auto_ = fromBytes(settings.shell_auto());
        ui_->shell_ask_ = fromBytes(settings.shell_ask());
        ui_->shell_allow_all_ = settings.shell_allow_all();
        ui_->delete_confirm_ = settings.delete_confirm();
        ui_->list_ignore_ = fromBytes(settings.list_ignore());
        if (settings.shell_timeout_sec() > 0) {
          ui_->shell_timeout_sec_ = settings.shell_timeout_sec();
        }
        ui_->shell_net_ = settings.shell_net();
        ui_->read_only_ = settings.read_only();
        ui_->allow_unsandboxed_shell_ = settings.allow_unsandboxed_shell();
      }
      if (settings.context_tokens() > 0) {
        ui_->context_tokens_ = settings.context_tokens();
        ui_->context_window_ = settings.context_tokens();
      }
      if (settings.max_steps() > 0) {
        ui_->max_steps_ = settings.max_steps();
      }
      emit ui_->settingsChanged();
      emit ui_->contextChanged();
    }
    if (ui_->dialog_id_ == 0 && ui_->dialogs_->rowCount() > 0) {
      const qint64 id = ui_->dialogs_->data(ui_->dialogs_->index(0), DialogModel::IdRole).toLongLong();
      ui_->openDialog(id);
    }
  });
}

void Desk::runOpen(qint64 id, std::uint64_t generation) {
  if(generation!=open_generation_.load())return;
  grpc::ClientContext ctx;
  bearer(ctx);
  arli::v1::OpenDialogRequest req;
  req.set_dialog_id(id);
  req.set_message_page_size(50);
  arli::v1::OpenDialogReply reply;
  const auto status=stub()->OpenDialog(&ctx, req, &reply);
  if (!status.ok()) {
    onUi([this,generation,error=fromBytes(status.error_message())]{if(generation==open_generation_.load()){
      ui_->status_text_=QStringLiteral("Не удалось открыть историю: ")+error;emit ui_->stateChanged();}});
    return;
  }
  QVector<MessageItem> messages;
  for (const auto& m : reply.messages()) {
    messages.push_back(MessageItem{m.id(), fromBytes(m.role()), fromBytes(m.text()), m.turn_id(), false});
  }
  QVector<TraceItem> trace;
  for (const auto& item : reply.last_trace()) {
    const QString kind = fromBytes(item.kind());
    if (kind == QLatin1String("status") || kind == QLatin1String("token") || kind == QLatin1String("done")) {
      continue;
    }
    TraceItem row;
    row.kind = (kind == QLatin1String("observation") && !item.ok()) ? QStringLiteral("obs_fail") : kind;
    row.text = fromBytes(item.text());
    row.tool = fromBytes(item.tool());
    row.args = fromBytes(item.args_json());
    row.diff = fromBytes(item.diff());
    row.ok = item.ok();
    row.durationMs = item.duration_ms();
    if (row.text.isEmpty() && row.tool.isEmpty()) {
      continue;
    }
    // Collapse legacy SSE thought crumbs (one row per token) into a single step.
    if (row.kind == QLatin1String("thought") && !trace.isEmpty() &&
        trace.back().kind == QLatin1String("thought") && row.text.size() < 64) {
      trace.back().text += row.text;
      continue;
    }
    trace.push_back(std::move(row));
  }
  bool active = false;
  {
    grpc::ClientContext tctx;
    bearer(tctx);
    arli::v1::ListTasksRequest lreq;
    lreq.set_dialog_id(id);
    lreq.set_limit(20);
    arli::v1::ListTasksReply lrep;
    QVector<TaskItem> tasks;
    if (stub()->ListTasks(&tctx, lreq, &lrep).ok()) {
      for (const auto& t : lrep.tasks()) {
        if (t.status() == "running" || t.status() == "queued") {
          active = true;
        }
        tasks.push_back(TaskItem{t.id(), t.turn_id(), fromBytes(t.status()), fromBytes(t.error()),
                                 fromBytes(t.created_at())});
      }
    }
    onUi([this, id, title = fromBytes(reply.dialog().title()), instructions = fromBytes(reply.dialog().instructions()), messages = std::move(messages),
          trace = std::move(trace), tasks = std::move(tasks), active, generation, next=reply.next_before_id()]() mutable {
      if(generation!=open_generation_.load())return;
      ui_->followId().store(active ? id : 0);
      ui_->dialog_id_ = id;
      ui_->dialog_instructions_ = instructions;
      ui_->dialog_title_ = title;
      ui_->messages_->setAll(std::move(messages));
      ui_->messages_->resetPaging(next);
      ui_->trace_->setAll(active ? QVector<TraceItem>{} : std::move(trace));
      ui_->tasks_->setAll(std::move(tasks));
      ui_->clearConfirm();
      ui_->clearAsk();
      ui_->shown_turn_ = 0;
      ui_->message_steps_.clear();
      emit ui_->stepsChanged();
      ui_->showInspector(QString(), QString());
      emit ui_->dialogChanged();
      if (active) {
        ui_->cancelFlag().store(false);
        ui_->setBusy(true);
        post([this, id] { runAttach(id); });
      } else {
        ui_->setBusy(false);
      }
      post([this, id] { runLoadModes(id); });
    });
  }
}

void Desk::runCreate() {
  grpc::ClientContext ctx;
  bearer(ctx);
  arli::v1::CreateDialogRequest req;
  req.set_title("Диалог");
  arli::v1::Dialog reply;
  const auto st = stub()->CreateDialog(&ctx, req, &reply);
  if (!st.ok() || reply.id() <= 0) {
    onUi([this, err = QString::fromStdString(st.error_message())]() {
      ui_->status_text_ = err.isEmpty() ? QStringLiteral("диалог не создан")
                                         : (QStringLiteral("диалог не создан: ") + err);
      emit ui_->stateChanged();
    });
    return;
  }
  const qint64 id = reply.id();
  onUi([this, id]() {
    ui_->setPage(0);
    ui_->openDialog(id);
    post([this] { runRefresh(); });
  });
}

void Desk::runRename(qint64 id, const std::string& title) {
  grpc::ClientContext ctx;
  bearer(ctx);
  arli::v1::RenameDialogRequest req;
  req.set_dialog_id(id);
  req.set_title(title);
  arli::v1::Dialog reply;
  const auto status=stub()->RenameDialog(&ctx, req, &reply);
  if (!status.ok()) {
    onUi([this,error=fromBytes(status.error_message())]{ui_->status_text_=QStringLiteral("Не удалось переименовать диалог: ")+error;emit ui_->stateChanged();});
    return;
  }
  onUi([this, id, title = fromBytes(reply.title())]() {
    if (ui_->dialog_id_ == id) {
      ui_->dialog_title_ = title;
      emit ui_->dialogChanged();
    }
  });
  runRefresh();
}

void Desk::runDelete(qint64 id) {
  grpc::ClientContext ctx;
  bearer(ctx);
  arli::v1::DeleteDialogRequest req;
  req.set_dialog_id(id);
  arli::v1::DeleteDialogReply reply;
  if (!stub()->DeleteDialog(&ctx, req, &reply).ok() || !reply.ok()) {
    return;
  }
  onUi([this, id]() {
    if (ui_->dialog_id_ == id) {
      ui_->followId().store(0);
      ui_->dialog_id_ = 0;
      ui_->dialog_title_.clear();
      ui_->dialog_instructions_.clear();
      ui_->messages_->clear();
      ui_->trace_->clear();
      ui_->tasks_->setAll({});
      ui_->setBusy(false);
      emit ui_->dialogChanged();
    }
  });
  runRefresh();
}
