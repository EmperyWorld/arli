#include "facade.hpp"
#include "desk.hpp"

#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

namespace {

QString utf(const std::string& s) {
  return QString::fromStdString(s);
}
QString oneLine(QString s, int max = 220) {
  s.replace(QLatin1Char('\n'), QLatin1Char(' '));
  return s.simplified().left(max);
}

QStringList toList(const google::protobuf::RepeatedPtrField<std::string>& in) {
  QStringList out;
  out.reserve(in.size());
  for (const auto& s : in) {
    out.push_back(utf(s));
  }
  return out;
}

QString confirmKindOf(const QString& command) {
  const QString c = command.toLower();
  if(c.startsWith(QLatin1String("revert_changeset ")))return QStringLiteral("revert");
  if (c.contains(QLatin1String("rm ")) || c.startsWith(QLatin1String("rm")) ||
      c.contains(QLatin1String("unlink"))) {
    return QStringLiteral("delete");
  }
  if (c.contains(QLatin1String("unzip")) || c.contains(QLatin1String("tar "))) {
    return QStringLiteral("extract");
  }
  return QStringLiteral("shell");
}

}  // namespace

void Desk::pump(grpc::ClientReader<arli::v1::TurnEvent>& reader, grpc::ClientContext& ctx, qint64 id) {
  arli::v1::TurnEvent ev;
  while (reader.Read(&ev)) {
    if (!follow(id)) {
      ctx.TryCancel();
      break;
    }
    if (ev.event_id()) {
      if (ev.event_id() <= stream_after_event_id_) continue;
      stream_after_event_id_ = ev.event_id(); stream_task_id_ = ev.task_id();
    }
    const qint64 turn = ev.turn_id();
    if (ev.has_token()) {
      onUi([this, id, delta = utf(ev.token().text())]() {
        if (ui_->followId().load() == id) {
          ui_->noteStreamDelta(delta.size());
          ui_->messages_->pushToken(delta);
        }
      });
    } else if (ev.has_thought()) {
      const QString raw = utf(ev.thought().text());
      const QString t = oneLine(raw, raw.size() < 64 ? 64 : 220);
      if (!t.isEmpty()) {
        onUi([this, id, t, preview = oneLine(raw, 160)]() {
          if (ui_->followId().load() == id) {
            ui_->status_text_ = preview;
            ui_->trace_->addOrMergeThought(t);
            emit ui_->stateChanged();
          }
        });
      }
    } else if (ev.has_action()) {
      onUi([this, id, tool = utf(ev.action().tool()), args = utf(ev.action().args_json())]() {
        if (ui_->followId().load() == id) {
          ui_->status_text_ = tool;
          ui_->trace_->add(TraceItem{QStringLiteral("action"), tool, tool, args, {}, true, 0});
          emit ui_->stateChanged();
        }
      });
    } else if (ev.has_observation()) {
      onUi([this, id, tool = utf(ev.observation().tool()), out = utf(ev.observation().output()),
            args = utf(ev.observation().args_json()), diff = utf(ev.observation().diff()),
            ok = ev.observation().ok(), dur = ev.observation().duration_ms()]() {
        if (ui_->followId().load() == id) {
          const QString kind = ok ? QStringLiteral("observation") : QStringLiteral("obs_fail");
          ui_->status_text_ = tool + (ok ? QStringLiteral(" готово") : QStringLiteral(" ошибка"));
          TraceItem row{kind, out, tool, args, diff, ok, dur};
          ui_->trace_->add(std::move(row));
          emit ui_->stateChanged();
        }
      });
    } else if (ev.has_confirm_needed()) {
      if (ev.event_id() && !ev.interaction_active()) continue;
      onUi([this, id, tok = utf(ev.confirm_needed().confirm_token()), cmd = utf(ev.confirm_needed().command()),
            cons = utf(ev.confirm_needed().consequence())]() {
        if (ui_->followId().load() != id) {
          return;
        }
        ui_->confirm_open_ = true;
        ui_->confirm_kind_ = confirmKindOf(cmd);
        ui_->confirm_token_ = tok;
        ui_->confirm_command_ = cmd;
        ui_->confirm_consequence_ = cons;
        ui_->trace_->add(TraceItem{QStringLiteral("confirm"), cmd, QStringLiteral("run_shell"), {}, {}, true, 0});
        emit ui_->confirmChanged();
      });
    } else if (ev.has_ask_user()) {
      if (ev.event_id() && !ev.interaction_active()) continue;
      QStringList options;
      for (const auto& o : ev.ask_user().options()) {
        options.push_back(utf(o));
      }
      onUi([this, id, q = utf(ev.ask_user().question()), question_id = ev.ask_user().question_id(), options]() {
        if (ui_->followId().load() != id) {
          return;
        }
        ui_->ask_open_ = true;
        ui_->ask_question_ = q;
        ui_->ask_question_id_ = question_id;
        ui_->ask_options_ = options;
        ui_->trace_->add(TraceItem{QStringLiteral("ask_user"), q, {}, {}, {}, true, 0});
        emit ui_->askChanged();
      });
    } else if (ev.has_context_use()) {
      onUi([this, id, used = ev.context_use().used_tokens(), window = ev.context_use().window_tokens(),
            compressed = ev.context_use().compressed(), reported = ev.context_use().provider_reported()]() {
        if (ui_->followId().load() == id) {
          ui_->noteContext(used, window, compressed, reported);
        }
      });
    } else if (ev.has_file_pass()) {
      onUi([this, id, paths = toList(ev.file_pass().paths()), done = toList(ev.file_pass().done()),
            reviewed = toList(ev.file_pass().reviewed()), hold = ev.file_pass().hold_for_write()]() {
        if (ui_->followId().load() == id) {
          ui_->applyFilePass(paths, done, hold, reviewed);
        }
      });
    } else if (ev.has_plan()) {
      onUi([this, id, steps = toList(ev.plan().steps()), cur = ev.plan().current(),
            failed = ev.plan().current_failed()]() {
        if (ui_->followId().load() == id) {
          ui_->applyPlan(steps, cur, failed);
        }
      });
    } else if (ev.has_status() || ev.has_queued() || ev.has_error()) {
      QString text;
      QString kind;
      if (ev.has_status()) {
        text = utf(ev.status().text());
      } else if (ev.has_queued()) {
        text = QStringLiteral("в очереди: ") + QString::number(ev.queued().position());
      } else {
        text = utf(ev.error().message());
        kind = QStringLiteral("error");
      }
      onUi([this, id, text, kind]() {
        if (ui_->followId().load() != id) {
          return;
        }
        ui_->status_text_ = text;
        if (!kind.isEmpty()) {
          ui_->trace_->add(TraceItem{kind, text, {}, {}, {}, false, 0});
        }
        emit ui_->stateChanged();
      });
    } else if (ev.has_done()) {
      const auto& done = ev.done();
      const int added = done.has_edit_stats() ? done.edit_stats().lines_added() : 0;
      const int removed = done.has_edit_stats() ? done.edit_stats().lines_removed() : 0;
      const int files = done.has_edit_stats() ? done.edit_stats().files_touched() : 0;
      onUi([this, id, turn, text = utf(done.assistant_text()), outcome = done.outcome(), verification = done.verification_state(), added, removed, files]() {
        if (ui_->followId().load() != id) {
          return;
        }
        ui_->messages_->finishStream(text, turn);
        ui_->noteTurnEditStats(added, removed, files);
        if (outcome == "failed" || outcome == "cancelled") {
          ui_->status_text_ = outcome == "failed" ? QStringLiteral("Задача не завершена") : QStringLiteral("Отменено");
          emit ui_->stateChanged();
        }
        if (outcome == "done" && (verification == "unverified" || verification == "partial")) {
          ui_->status_text_ = QStringLiteral("Ход завершён, проверка результата неполная");
          emit ui_->stateChanged();
        }
        ui_->resetStreamChars();
        ui_->clearConfirm();
        ui_->clearAsk();
      });
    }
  }
}

void Desk::runTurnTrace(qint64 dialogId, qint64 turnId) {
  QVariantList steps;
  qint64 after=0;
  for(int page=0;page<10;++page){
    grpc::ClientContext ctx;bearer(ctx);
    ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
    arli::v1::GetTurnTraceRequest req;req.set_dialog_id(dialogId);req.set_turn_id(turnId);
    req.set_after_id(after);req.set_page_size(100);
    arli::v1::GetTurnTraceReply reply;
    if(!stub()->GetTurnTrace(&ctx,req,&reply).ok())return;
    for (const auto& item : reply.items()) {
    const QString kind = utf(item.kind());
    if(kind==QLatin1String("done")||kind==QLatin1String("changeset")){
      const auto doc=QJsonDocument::fromJson(QByteArray::fromStdString(item.args_json()));
      const auto changeset=doc.object().value(QStringLiteral("changesetId")).toString();
      if(!changeset.isEmpty())steps.push_back(QVariantMap{{QStringLiteral("kind"),QStringLiteral("changeset")},{QStringLiteral("changeset"),changeset},{QStringLiteral("text"),QStringLiteral("Сохранён набор правок")}});
    }
    if (kind == QLatin1String("status") || kind == QLatin1String("token") || kind == QLatin1String("done") || kind == QLatin1String("changeset")) {
      continue;
    }
    QVariantMap row;
    row.insert(QStringLiteral("kind"), kind);
    row.insert(QStringLiteral("text"), utf(item.text()));
    row.insert(QStringLiteral("tool"), utf(item.tool()));
    row.insert(QStringLiteral("ok"), item.ok());
    steps.push_back(row);
    }
    if(reply.next_after_id()<=after)break;
    after=reply.next_after_id();
  }
  onUi([this, dialogId, turnId, steps]() {
    if(ui_->dialog_id_!=dialogId||ui_->shown_turn_!=turnId)return;
    ui_->message_steps_ = steps;
    emit ui_->stepsChanged();
  });
}
