#include "facade.hpp"

#include "desk.hpp"

#include "auth.hpp"
#include "paths.hpp"
#include "secrets.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <cmath>

AgentFacade::AgentFacade(QObject* parent)
    : QObject(parent),
      dialogs_(new DialogModel(this)),
      messages_(new MessageModel(this)),
      trace_(new TraceModel(this)),
      tasks_(new TaskModel(this)) {
  workdir_ = QString::fromStdString((arli::state_root() / "workspace").string());
  desk_ = std::make_unique<Desk>(this);
  connect(dialogs_,&DialogModel::pageRequested,this,[this](QString token,quint64 generation){desk_->requestDialogPage(std::move(token),generation);});
  connect(messages_,&MessageModel::olderRequested,this,[this](qint64 before,quint64 generation){
    desk_->requestOlder(dialog_id_,before,generation);
  });
  connect(&health_timer_, &QTimer::timeout, this, [this] { desk_->post([this] { desk_->runHealth(); }); });
  health_timer_.start(2000);
  refresh();
}

AgentFacade::~AgentFacade() {
  alive_->store(false);
  health_timer_.stop();
  follow_id_.store(0);
  desk_->shutdown();
}

void AgentFacade::setDraft(const QString& v) {
  if (draft_ == v) return;
  draft_ = v;
  emit draftChanged();
}

void AgentFacade::setDialogFilter(const QString& v) {
  dialogs_->setFilter(v);
  emit dialogFilterChanged();
}

void AgentFacade::setLlmBaseUrl(const QString& v) {
  if (llm_base_url_ == v) return;
  llm_base_url_ = v;
  emit settingsChanged();
}

void AgentFacade::setLlmModel(const QString& v) {
  if (llm_model_ == v) return;
  llm_model_ = v;
  emit settingsChanged();
}

void AgentFacade::setLlmProfile(const QString& v) {
  QString p = v.trimmed().toLower();
  if (p.isEmpty() || p == QLatin1String("gemma")) {
    p = QStringLiteral("gemma4");
  }
  if (p == QLatin1String("qwen") || p == QLatin1String("qwen3.8") || p == QLatin1String("qwen38")) {
    p = QStringLiteral("qwen3");
  }
  if (llm_profile_ == p) return;
  llm_profile_ = p;
  emit settingsChanged();
}

void AgentFacade::setLlmTemperature(double value) {
  if (!std::isfinite(value) || (value != -1.0 && (value < 0.0 || value > 2.0)) || llm_temperature_ == value) return;
  llm_temperature_ = value;
  emit settingsChanged();
}

void AgentFacade::setWorkdir(const QString& v) {
  if (workdir_ == v) return;
  workdir_ = v;
  emit settingsChanged();
}

void AgentFacade::setTurnTimeout(int v) {
  if (turn_timeout_ == v) return;
  turn_timeout_ = v;
  emit settingsChanged();
}

void AgentFacade::setIdleTimeout(int v) {
  if (idle_timeout_ == v) return;
  idle_timeout_ = v;
  emit settingsChanged();
}

void AgentFacade::setCancelGrace(int v) {
  if (cancel_grace_ == v) return;
  cancel_grace_ = v;
  emit settingsChanged();
}

void AgentFacade::setShellAuto(const QString& v) {
  if (shell_auto_ == v) return;
  shell_auto_ = v;
  emit settingsChanged();
}

void AgentFacade::setShellAsk(const QString& v) {
  if (shell_ask_ == v) return;
  shell_ask_ = v;
  emit settingsChanged();
}

void AgentFacade::setShellAllowAll(bool v) {
  if (shell_allow_all_ == v) return;
  shell_allow_all_ = v;
  emit settingsChanged();
}

void AgentFacade::setDeleteConfirm(bool v) {
  if (delete_confirm_ == v) return;
  delete_confirm_ = v;
  emit settingsChanged();
}

void AgentFacade::setListIgnore(const QString& v) {
  if (list_ignore_ == v) return;
  list_ignore_ = v;
  emit settingsChanged();
}

void AgentFacade::setContextTokens(int v) {
  if (context_tokens_ == v) return;
  context_tokens_ = v;
  context_window_ = v;
  emit settingsChanged();
  emit contextChanged();
}

void AgentFacade::setMaxSteps(int v) {
  if (max_steps_ == v) return;
  max_steps_ = v;
  emit settingsChanged();
}

void AgentFacade::setShellTimeoutSec(int v) {
  if (shell_timeout_sec_ == v) return;
  shell_timeout_sec_ = v;
  emit settingsChanged();
}

void AgentFacade::setShellNet(bool v) {
  if (shell_net_ == v) return;
  shell_net_ = v;
  emit settingsChanged();
}

void AgentFacade::setReadOnly(bool v) {
  if (read_only_ == v) return;
  read_only_ = v;
  emit settingsChanged();
}

void AgentFacade::setAllowUnsandboxedShell(bool v) {
  if (allow_unsandboxed_shell_ == v) return;
  allow_unsandboxed_shell_ = v;
  emit settingsChanged();
}

void AgentFacade::setPage(int v) {
  if (page_ == v) return;
  page_ = v;
  emit pageChanged();
}

void AgentFacade::setHelpOpen(bool v) {
  if (help_open_ == v) return;
  help_open_ = v;
  emit helpChanged();
}

void AgentFacade::setBusy(bool v) {
  if (busy_ == v) return;
  busy_ = v;
  emit busyChanged();
}

void AgentFacade::clearConfirm() {
  confirm_open_ = false;
  confirm_token_.clear();
  confirm_command_.clear();
  confirm_consequence_.clear();
  emit confirmChanged();
}

void AgentFacade::clearAsk() {
  ask_open_ = false;
  ask_question_.clear();
  ask_question_id_.clear();
  ask_options_.clear();
  emit askChanged();
}

void AgentFacade::showInspector(const QString& title, const QString& body, const QString& diff) {
  desk_->stopJobLog();job_log_selected_=false;
  inspector_title_ = title;
  inspector_body_ = body;
  inspector_diff_ = diff;
  emit inspectorChanged();
}

void AgentFacade::noteContext(int used, int window, bool compressed, bool reported) {
  context_reported_ = reported;
  context_used_ = used;
  if (window > 0) {
    context_window_ = window;
  }
  context_compressed_ = compressed;
  emit contextChanged();
}

void AgentFacade::markDirty() {
  if (settings_dirty_) {
    return;
  }
  settings_dirty_ = true;
  emit settingsChanged();
}

void AgentFacade::openJobLog(int index) {
  if (index < 0 || index >= jobs_.size()) {
    return;
  }
  const QVariantMap row = jobs_.at(index).toMap();
  showInspector(row.value(QStringLiteral("command")).toString(), QStringLiteral("Подключение к выводу…"));
  job_log_selected_=true;emit inspectorChanged();desk_->startJobLog(row);
}

void AgentFacade::stopJobLog(){desk_->stopJobLog();job_log_selected_=false;emit inspectorChanged();}

void AgentFacade::showNote(const QString& title, const QString& body) {
  showInspector(title, body);
}

namespace {

QString filePassHeadOf(const QStringList& paths, const QStringList& done) {
  for (const QString& p : paths) {
    if (!done.contains(p)) {
      return p;
    }
  }
  return {};
}

}  // namespace

QVariantMap AgentFacade::filePass() const {
  const QString head = filePassHeadOf(file_pass_paths_, file_pass_done_ + file_pass_reviewed_);
  QVariantMap m;
  m.insert(QStringLiteral("paths"), file_pass_paths_);
  m.insert(QStringLiteral("done"), file_pass_done_);
  m.insert(QStringLiteral("hold"), file_pass_hold_);
  m.insert(QStringLiteral("active"), !file_pass_paths_.isEmpty());
  m.insert(QStringLiteral("reviewed"), file_pass_reviewed_);
  m.insert(QStringLiteral("doneCount"), file_pass_done_.size() + file_pass_reviewed_.size());
  m.insert(QStringLiteral("head"), head);
  return m;
}

QVariantMap AgentFacade::plan() const {
  QVariantMap m;
  m.insert(QStringLiteral("steps"), plan_steps_);
  m.insert(QStringLiteral("current"), plan_current_);
  m.insert(QStringLiteral("failed"), plan_failed_);
  m.insert(QStringLiteral("active"), !plan_steps_.isEmpty());
  return m;
}

void AgentFacade::applyFilePass(const QStringList& paths, const QStringList& done, bool hold, const QStringList& reviewed) {
  file_pass_paths_ = paths;
  file_pass_done_ = done;
  file_pass_reviewed_ = reviewed;
  file_pass_hold_ = hold;
  emit modesChanged();
}

void AgentFacade::applyPlan(const QStringList& steps, int current, bool failed) {
  plan_steps_ = steps;
  plan_current_ = current;
  plan_failed_ = failed;
  emit modesChanged();
}

void AgentFacade::noteStreamDelta(int chars) {
  if (chars <= 0) {
    return;
  }
  stream_chars_ += chars;
  emit streamChanged();
}

void AgentFacade::resetStreamChars() {
  if (stream_chars_ == 0) {
    return;
  }
  stream_chars_ = 0;
  emit streamChanged();
}

void AgentFacade::noteTurnEditStats(int added, int removed, int files) {
  if (added <= 0 && removed <= 0 && files <= 0) {
    turn_edit_summary_.clear();
    emit turnStatsChanged();
    return;
  }
  turn_edit_summary_ = QStringLiteral("+") + QString::number(added) + QStringLiteral(" −") +
                       QString::number(removed) + QStringLiteral(" · ") + QString::number(files) +
                       (files == 1 ? QStringLiteral(" файл") : QStringLiteral(" файла"));
  emit turnStatsChanged();
}

void AgentFacade::clearFilePass() {
  const qint64 id = dialog_id_;
  if (id <= 0) {
    return;
  }
  desk_->post([this, id] { desk_->runClearFilePass(id); });
}

void AgentFacade::clearPlan() {
  const qint64 id = dialog_id_;
  if (id <= 0) {
    return;
  }
  desk_->post([this, id] { desk_->runClearPlan(id); });
}

void AgentFacade::refresh() {
  desk_->post([this] { desk_->runRefresh(); });
}

void AgentFacade::createDialog() {
  desk_->post([this] { desk_->runCreate(); });
}

void AgentFacade::openDialog(qint64 id) {
  if (id <= 0) return;
  follow_id_.store(0);
  desk_->cancelActive();
  desk_->requestOpen(id);
}

void AgentFacade::renameDialog(const QString& title) {
  const qint64 id = dialog_id_;
  if (id <= 0 || title.trimmed().isEmpty()) return;
  desk_->post([this, id, t = title.trimmed().toStdString()] { desk_->runRename(id, t); });
}

void AgentFacade::deleteDialog() {
  const qint64 id = dialog_id_;
  if (id <= 0) return;
  follow_id_.store(0);
  desk_->cancelActive();
  desk_->post([this, id] { desk_->runDelete(id); });
}

void AgentFacade::send() {
  if (busy_ || draft_.trimmed().isEmpty() || dialog_id_ == 0) {
    if (dialog_id_ == 0) {
      status_text_ = QStringLiteral("сначала выбери диалог");
      emit stateChanged();
    }
    return;
  }
  const QString text = draft_.trimmed();
  if (text.size() > 4000) {
    status_text_ = QStringLiteral("Слишком длинно: максимум 4000 знаков.");
    emit stateChanged();
    return;
  }
  draft_.clear();
  emit draftChanged();
  setBusy(true);
  cancel_flag_.store(false);
  follow_id_.store(dialog_id_);
  resetStreamChars();
  turn_edit_summary_.clear();
  emit turnStatsChanged();
  trace_->clear();
  clearConfirm();
  clearAsk();
  messages_->add(MessageItem{0, QStringLiteral("user"), text, 0, false});
  const qint64 id = dialog_id_;
  desk_->post([this, id, t = text.toStdString()] { desk_->runSend(id, t); });
}

void AgentFacade::cancel() {
  cancel_flag_.store(true);
  desk_->cancelActive();
  const qint64 id = dialog_id_;
  desk_->postControl([this, id] {
    auto* stub = desk_->stub();
    if (!stub) return;
    grpc::ClientContext rpc;
    arli::auth::bearer(rpc, arli::secrets::ui_token());
    rpc.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    arli::v1::CancelRequest req;
    req.set_dialog_id(id);
    arli::v1::CancelReply reply;
    const auto status = stub->Cancel(&rpc, req, &reply);
    desk_->reportControl(status, reply.ok());
  });
  clearConfirm();
}

void AgentFacade::hardStop() {
  cancel_flag_.store(true);
  desk_->cancelActive();
  const qint64 id = dialog_id_;
  desk_->postControl([this, id] {
    auto* stub = desk_->stub();
    if (!stub || id <= 0) return;
    grpc::ClientContext rpc;
    arli::auth::bearer(rpc, arli::secrets::ui_token());
    rpc.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    arli::v1::HardStopRequest req;
    req.set_dialog_id(id);
    arli::v1::HardStopReply reply;
    const auto status = stub->HardStop(&rpc, req, &reply);
    desk_->reportControl(status, reply.ok());
  });
  clearConfirm();
}

void AgentFacade::saveSettings() {
  desk_->post([this] { desk_->runSaveSettings(); });
}

void AgentFacade::setApiKey(const QString& key) {
  pending_api_key_ = key.trimmed();
  clear_api_key_ = false;
  if (!pending_api_key_.isEmpty()) {
    saveSettings();
  }
}

void AgentFacade::clearApiKey() {
  clear_api_key_ = true;
  pending_api_key_.clear();
  saveSettings();
}

void AgentFacade::confirmDanger(bool accept) {
  if (!confirm_open_ || dialog_id_ == 0) return;
  const qint64 id = dialog_id_;
  const auto token = confirm_token_.toStdString();
  clearConfirm();
  desk_->postControl([this, id, token, accept] {
    auto* stub = desk_->stub();
    if (!stub) return;
    grpc::ClientContext rpc;
    arli::auth::bearer(rpc, arli::secrets::ui_token());
    rpc.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    arli::v1::ConfirmDangerRequest req;
    req.set_dialog_id(id);
    req.set_confirm_token(token);
    req.set_accept(accept);
    arli::v1::ConfirmDangerReply reply;
    const auto status = stub->ConfirmDanger(&rpc, req, &reply);
    desk_->reportControl(status, reply.ok());
  });
}

void AgentFacade::answerAsk(const QString& answer) {
  if (!ask_open_ || dialog_id_ == 0 || answer.trimmed().isEmpty()) return;
  const qint64 id = dialog_id_;
  const auto text = answer.trimmed().toStdString();
  const auto question_id = ask_question_id_;
  clearAsk();
  desk_->postControl([this, id, text, question_id] {
    auto* stub = desk_->stub();
    if (!stub) return;
    grpc::ClientContext rpc;
    arli::auth::bearer(rpc, arli::secrets::ui_token());
    rpc.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    arli::v1::AnswerAskUserRequest req;
    req.set_dialog_id(id);
    req.set_answer(text);
    req.set_question_id(question_id);
    arli::v1::AnswerAskUserReply reply;
    const auto status = stub->AnswerAskUser(&rpc, req, &reply);
    desk_->reportControl(status, reply.ok());
  });
}

void AgentFacade::stopJob(qint64 id) { desk_->post([this, id] { desk_->runStopJob(id); }); }

void AgentFacade::stopAllJobs() {
  for (const auto& row : jobs_) {
    const auto map = row.toMap();
    if (map.value(QStringLiteral("status")).toString() == QLatin1String("running")) {
      stopJob(map.value(QStringLiteral("id")).toLongLong());
    }
  }
}

void AgentFacade::copyText(const QString& text) {
  if (auto* clipboard = QGuiApplication::clipboard()) {
    clipboard->setText(text);
    emit textCopied();
  }
}

void AgentFacade::showTurn(qint64 turnId) {
  if (turnId <= 0 || dialog_id_ == 0) {
    return;
  }
  if (shown_turn_ == turnId) {
    shown_turn_ = 0;
    message_steps_.clear();
    emit stepsChanged();
    return;
  }
  shown_turn_ = turnId;
  message_steps_.clear();
  emit stepsChanged();
  const qint64 dialog = dialog_id_;
  desk_->post([this, dialog, turnId] { desk_->runTurnTrace(dialog, turnId); });
}

void AgentFacade::selectTrace(int row) {
  const TraceItem item = trace_->at(row);
  if (item.text.isEmpty() && item.tool.isEmpty() && item.diff.isEmpty()) {
    showInspector({}, {}, {});
    return;
  }
  const auto label = [](const QString& kind) {
    if (kind == QLatin1String("thought")) return QStringLiteral("Мысль");
    if (kind == QLatin1String("action")) return QStringLiteral("Делает");
    if (kind == QLatin1String("observation")) return QStringLiteral("Результат");
    if (kind == QLatin1String("obs_fail")) return QStringLiteral("Ошибка");
    if (kind == QLatin1String("error")) return QStringLiteral("Сбой");
    if (kind == QLatin1String("confirm")) return QStringLiteral("Подтверждение");
    if (kind == QLatin1String("ask_user")) return QStringLiteral("Вопрос");
    return kind;
  };
  QString title = item.tool.isEmpty() ? label(item.kind) : (label(item.kind) + QStringLiteral(" · ") + item.tool);
  if (item.durationMs > 0) {
    title += QStringLiteral(" · ") + QString::number(item.durationMs) + QStringLiteral(" ms");
  }
  QString body = item.text;
  if (!item.args.isEmpty()) {
    body += QStringLiteral("\n\n") + item.args;
  }
  showInspector(title, body, item.diff);
}
