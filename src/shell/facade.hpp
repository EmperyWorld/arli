#pragma once
#include "models.hpp"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <memory>
class Desk;
// QML-facing desk. gRPC lives in Desk; this object only holds UI state.
class AgentFacade : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool daemonUp READ daemonUp NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  Q_PROPERTY(QString sliceText READ sliceText NOTIFY stateChanged)
  Q_PROPERTY(QString versionText READ versionText NOTIFY stateChanged)
  Q_PROPERTY(QString workerState READ workerState NOTIFY stateChanged)
  Q_PROPERTY(int workerPid READ workerPid NOTIFY stateChanged)
  Q_PROPERTY(int queueDepth READ queueDepth NOTIFY stateChanged)
  Q_PROPERTY(qint64 dialogId READ dialogId NOTIFY dialogChanged)
  Q_PROPERTY(QString dialogInstructions READ dialogInstructions NOTIFY dialogChanged)
  Q_PROPERTY(QString dialogTitle READ dialogTitle NOTIFY dialogChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
  Q_PROPERTY(QString dialogFilter READ dialogFilter WRITE setDialogFilter NOTIFY dialogFilterChanged)
  Q_PROPERTY(QString llmBaseUrl READ llmBaseUrl WRITE setLlmBaseUrl NOTIFY settingsChanged)
  Q_PROPERTY(QString llmModel READ llmModel WRITE setLlmModel NOTIFY settingsChanged)
  Q_PROPERTY(QString llmProfile READ llmProfile WRITE setLlmProfile NOTIFY settingsChanged)
  Q_PROPERTY(double llmTemperature READ llmTemperature WRITE setLlmTemperature NOTIFY settingsChanged)
  Q_PROPERTY(bool llmReachable READ llmReachable NOTIFY stateChanged)
  Q_PROPERTY(QString workdir READ workdir WRITE setWorkdir NOTIFY settingsChanged)
  Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY settingsChanged)
  Q_PROPERTY(int turnTimeout READ turnTimeout WRITE setTurnTimeout NOTIFY settingsChanged)
  Q_PROPERTY(int idleTimeout READ idleTimeout WRITE setIdleTimeout NOTIFY settingsChanged)
  Q_PROPERTY(int cancelGrace READ cancelGrace WRITE setCancelGrace NOTIFY settingsChanged)
  Q_PROPERTY(QString shellAuto READ shellAuto WRITE setShellAuto NOTIFY settingsChanged)
  Q_PROPERTY(QString shellAsk READ shellAsk WRITE setShellAsk NOTIFY settingsChanged)
  Q_PROPERTY(bool shellAllowAll READ shellAllowAll WRITE setShellAllowAll NOTIFY settingsChanged)
  Q_PROPERTY(bool deleteConfirm READ deleteConfirm WRITE setDeleteConfirm NOTIFY settingsChanged)
  Q_PROPERTY(QString listIgnore READ listIgnore WRITE setListIgnore NOTIFY settingsChanged)
  Q_PROPERTY(int contextTokens READ contextTokens WRITE setContextTokens NOTIFY settingsChanged)
  Q_PROPERTY(int maxSteps READ maxSteps WRITE setMaxSteps NOTIFY settingsChanged)
  Q_PROPERTY(int shellTimeoutSec READ shellTimeoutSec WRITE setShellTimeoutSec NOTIFY settingsChanged)
  Q_PROPERTY(bool shellNet READ shellNet WRITE setShellNet NOTIFY settingsChanged)
  Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY settingsChanged)
  Q_PROPERTY(bool allowUnsandboxedShell READ allowUnsandboxedShell WRITE setAllowUnsandboxedShell NOTIFY settingsChanged)
  Q_PROPERTY(bool contextReported READ contextReported NOTIFY contextChanged)
  Q_PROPERTY(int contextUsed READ contextUsed NOTIFY contextChanged)
  Q_PROPERTY(int contextWindow READ contextWindow NOTIFY contextChanged)
  Q_PROPERTY(bool contextCompressed READ contextCompressed NOTIFY contextChanged)
  Q_PROPERTY(QVariantMap filePass READ filePass NOTIFY modesChanged)
  Q_PROPERTY(QVariantMap plan READ plan NOTIFY modesChanged)
  Q_PROPERTY(int streamChars READ streamChars NOTIFY streamChanged)
  Q_PROPERTY(QString turnEditSummary READ turnEditSummary NOTIFY turnStatsChanged)
  Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
  Q_PROPERTY(bool settingsDirty READ settingsDirty NOTIFY settingsChanged)
  Q_PROPERTY(QString settingsState READ settingsState NOTIFY settingsChanged)
  Q_PROPERTY(bool confirmOpen READ confirmOpen NOTIFY confirmChanged)
  Q_PROPERTY(QString confirmKind READ confirmKind NOTIFY confirmChanged)
  Q_PROPERTY(QString confirmToken READ confirmToken NOTIFY confirmChanged)
  Q_PROPERTY(QString confirmCommand READ confirmCommand NOTIFY confirmChanged)
  Q_PROPERTY(QString confirmConsequence READ confirmConsequence NOTIFY confirmChanged)
  Q_PROPERTY(bool askOpen READ askOpen NOTIFY askChanged)
  Q_PROPERTY(QString askQuestion READ askQuestion NOTIFY askChanged)
  Q_PROPERTY(QStringList askOptions READ askOptions NOTIFY askChanged)
  Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
  Q_PROPERTY(bool helpOpen READ helpOpen WRITE setHelpOpen NOTIFY helpChanged)
  Q_PROPERTY(bool jobLogSelected READ jobLogSelected NOTIFY inspectorChanged)
  Q_PROPERTY(QString inspectorTitle READ inspectorTitle NOTIFY inspectorChanged)
  Q_PROPERTY(QString inspectorBody READ inspectorBody NOTIFY inspectorChanged)
  Q_PROPERTY(QString inspectorDiff READ inspectorDiff NOTIFY inspectorChanged)
  Q_PROPERTY(qint64 shownTurn READ shownTurn NOTIFY stepsChanged)
  Q_PROPERTY(QVariantList messageSteps READ messageSteps NOTIFY stepsChanged)
  Q_PROPERTY(DialogModel* dialogs READ dialogs CONSTANT)
  Q_PROPERTY(MessageModel* messages READ messages CONSTANT)
  Q_PROPERTY(TraceModel* trace READ trace CONSTANT)
  Q_PROPERTY(TaskModel* tasks READ tasks CONSTANT)
 public:
  explicit AgentFacade(QObject* parent = nullptr);
  ~AgentFacade() override;
  bool daemonUp() const { return daemon_up_; }
  QString statusText() const { return status_text_; }
  QString sliceText() const { return slice_text_; }
  QString versionText() const { return version_text_; }
  QString workerState() const { return worker_state_; }
  int workerPid() const { return worker_pid_; }
  int queueDepth() const { return queue_depth_; }
  qint64 dialogId() const { return dialog_id_; }
  QString dialogInstructions() const { return dialog_instructions_; }
  QString dialogTitle() const { return dialog_title_; }
  bool busy() const { return busy_; }
  QString draft() const { return draft_; }
  void setDraft(const QString& v);
  QString dialogFilter() const { return dialogs_->filter(); }
  void setDialogFilter(const QString& v);
  QString llmBaseUrl() const { return llm_base_url_; }
  void setLlmBaseUrl(const QString& v);
  QString llmModel() const { return llm_model_; }
  void setLlmModel(const QString& v);
  QString llmProfile() const { return llm_profile_; }
  void setLlmProfile(const QString& v);
  double llmTemperature() const { return llm_temperature_; }
  void setLlmTemperature(double value);
  bool llmReachable() const { return llm_reachable_; }
  QString workdir() const { return workdir_; }
  void setWorkdir(const QString& v);
  bool hasApiKey() const { return has_api_key_; }
  int turnTimeout() const { return turn_timeout_; }
  void setTurnTimeout(int v);
  int idleTimeout() const { return idle_timeout_; }
  void setIdleTimeout(int v);
  int cancelGrace() const { return cancel_grace_; }
  void setCancelGrace(int v);
  QString shellAuto() const { return shell_auto_; }
  void setShellAuto(const QString& v);
  QString shellAsk() const { return shell_ask_; }
  void setShellAsk(const QString& v);
  bool shellAllowAll() const { return shell_allow_all_; }
  void setShellAllowAll(bool v);
  bool deleteConfirm() const { return delete_confirm_; }
  void setDeleteConfirm(bool v);
  QString listIgnore() const { return list_ignore_; }
  void setListIgnore(const QString& v);
  int contextTokens() const { return context_tokens_; }
  void setContextTokens(int v);
  int maxSteps() const { return max_steps_; }
  void setMaxSteps(int v);
  int shellTimeoutSec() const { return shell_timeout_sec_; }
  void setShellTimeoutSec(int v);
  bool shellNet() const { return shell_net_; }
  void setShellNet(bool v);
  bool readOnly() const { return read_only_; }
  void setReadOnly(bool v);
  bool allowUnsandboxedShell() const { return allow_unsandboxed_shell_; }
  void setAllowUnsandboxedShell(bool v);
  bool contextReported() const { return context_reported_; }
  int contextUsed() const { return context_used_; }
  int contextWindow() const { return context_window_; }
  bool contextCompressed() const { return context_compressed_; }
  void noteContext(int used, int window, bool compressed, bool reported = false);
  QVariantMap filePass() const;
  QVariantMap plan() const;
  void applyFilePass(const QStringList& paths, const QStringList& done, bool hold, const QStringList& reviewed = {});
  void applyPlan(const QStringList& steps, int current, bool failed);
  int streamChars() const { return stream_chars_; }
  QString turnEditSummary() const { return turn_edit_summary_; }
  void noteStreamDelta(int chars);
  void resetStreamChars();
  void noteTurnEditStats(int added, int removed, int files);
  QVariantList jobs() const { return jobs_; }
  bool settingsDirty() const { return settings_dirty_; }
  QString settingsState() const { return settings_state_; }
  Q_INVOKABLE void markDirty();
  Q_INVOKABLE void openJobLog(int index);
  Q_INVOKABLE void stopJobLog();
  bool jobLogSelected() const { return job_log_selected_; }
  Q_INVOKABLE void showNote(const QString& title, const QString& body);
  bool confirmOpen() const { return confirm_open_; }
  QString confirmKind() const { return confirm_kind_; }
  QString confirmToken() const { return confirm_token_; }
  QString confirmCommand() const { return confirm_command_; }
  QString confirmConsequence() const { return confirm_consequence_; }
  bool askOpen() const { return ask_open_; }
  QString askQuestion() const { return ask_question_; }
  QStringList askOptions() const { return ask_options_; }
  int page() const { return page_; }
  void setPage(int v);
  bool helpOpen() const { return help_open_; }
  void setHelpOpen(bool v);
  QString inspectorTitle() const { return inspector_title_; }
  QString inspectorBody() const { return inspector_body_; }
  QString inspectorDiff() const { return inspector_diff_; }
  qint64 shownTurn() const { return shown_turn_; }
  QVariantList messageSteps() const { return message_steps_; }
  DialogModel* dialogs() const { return dialogs_; }
  MessageModel* messages() const { return messages_; }
  TraceModel* trace() const { return trace_; }
  TaskModel* tasks() const { return tasks_; }
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void createDialog();
  Q_INVOKABLE void openDialog(qint64 id);
  Q_INVOKABLE void saveDialogInstructions(const QString& text);
  Q_INVOKABLE void renameDialog(const QString& title);
  Q_INVOKABLE void deleteDialog();
  Q_INVOKABLE void send();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void hardStop();
  Q_INVOKABLE void saveSettings();
  Q_INVOKABLE void setApiKey(const QString& key);
  Q_INVOKABLE void clearApiKey();
  Q_INVOKABLE void confirmDanger(bool accept);
  Q_INVOKABLE void answerAsk(const QString& answer);
  Q_INVOKABLE void selectTrace(int row);
  Q_INVOKABLE void showTurn(qint64 turnId);
  Q_INVOKABLE void revertTurn(const QString& changeset);
  Q_INVOKABLE void copyText(const QString& text);
  Q_INVOKABLE void stopJob(qint64 id);
  Q_INVOKABLE void stopAllJobs();
  Q_INVOKABLE void clearFilePass();
  Q_INVOKABLE void clearPlan();
  std::shared_ptr<std::atomic<bool>> alive() const { return alive_; }
  std::atomic<qint64>& followId() { return follow_id_; }
  std::atomic<bool>& cancelFlag() { return cancel_flag_; }
 signals:
  void textCopied();
  void stateChanged();
  void dialogChanged();
  void busyChanged();
  void draftChanged();
  void dialogFilterChanged();
  void settingsChanged();
  void confirmChanged();
  void askChanged();
  void pageChanged();
  void helpChanged();
  void inspectorChanged();
  void stepsChanged();
  void jobsChanged();
  void contextChanged();
  void modesChanged();
  void streamChanged();
  void turnStatsChanged();
 private:
  friend class Desk;
  void setBusy(bool v);
  void clearConfirm();
  void clearAsk();
  void showInspector(const QString& title, const QString& body, const QString& diff = {});
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  std::unique_ptr<Desk> desk_;
  QTimer health_timer_;
  DialogModel* dialogs_;
  MessageModel* messages_;
  TraceModel* trace_;
  TaskModel* tasks_;
  bool daemon_up_ = false;
  QString status_text_ = QStringLiteral("нет демона");
  QString slice_text_;
  QString version_text_;
  QString worker_state_ = QStringLiteral("none");
  int worker_pid_ = 0;
  int queue_depth_ = 0;
  qint64 dialog_id_ = 0;
  QString dialog_title_, dialog_instructions_;
  bool busy_ = false;
  QString draft_;
  QString llm_base_url_ = QStringLiteral("http://127.0.0.1:8080/v1");
  QString llm_model_;
  QString llm_profile_ = QStringLiteral("gemma4");
  double llm_temperature_ = -1.0;
  bool llm_reachable_ = false;
  QString workdir_;
  bool has_api_key_ = false;
  bool clear_api_key_ = false;
  QString pending_api_key_;
  int turn_timeout_ = 2700, idle_timeout_ = 600, cancel_grace_ = 10;
  QString shell_auto_;
  QString shell_ask_;
  bool shell_allow_all_ = false;
  bool delete_confirm_ = true;
  QString list_ignore_;
  int context_tokens_ = 16384;
  int max_steps_ = 64;
  int shell_timeout_sec_ = 120;
  bool shell_net_ = false;
  bool read_only_ = false;
  bool allow_unsandboxed_shell_ = false;
  bool context_reported_ = false;
  int context_used_ = 0;
  int context_window_ = 16384;
  bool context_compressed_ = false;
  QStringList file_pass_paths_, file_pass_done_, file_pass_reviewed_;
  bool file_pass_hold_ = false;
  QStringList plan_steps_;
  int plan_current_ = 0;
  bool plan_failed_ = false;
  int stream_chars_ = 0;
  QString turn_edit_summary_;
  QVariantList jobs_;
  bool settings_dirty_ = false;
  QString settings_state_ = QStringLiteral("ещё не записывали в этом окне");
  bool confirm_open_ = false;
  QString confirm_kind_ = QStringLiteral("shell");
  QString confirm_token_;
  QString confirm_command_;
  QString confirm_consequence_;
  bool ask_open_ = false;
  QString ask_question_;
  std::string ask_question_id_;
  QStringList ask_options_;
  int page_ = 0;
  bool help_open_ = false;
  QString inspector_title_, inspector_body_, inspector_diff_;bool job_log_selected_=false;
  qint64 shown_turn_ = 0;
  QVariantList message_steps_;
  std::atomic<bool> cancel_flag_{false};
  std::atomic<qint64> follow_id_{0};
};
