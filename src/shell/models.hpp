#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QTimer>

// List models for the shell. Roles are the names QML delegates read.

struct DialogItem {
  qint64 id = 0;
  QString title;
  QString updatedAt;
};

struct MessageItem {
  qint64 id = 0;
  QString speaker;  // user | assistant
  QString body;
  qint64 turnId = 0;
  bool streaming = false;
};

struct TraceItem {
  QString kind;
  QString text;
  QString tool;
  QString args;
  QString diff;
  bool ok = true;
  qint64 durationMs = 0;
};

struct TaskItem {
  qint64 id = 0;
  qint64 turnId = 0;
  QString status;
  QString error;
  QString createdAt;
};

class DialogModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool hasMore READ hasMore NOTIFY pagingChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY pagingChanged)
  Q_PROPERTY(QString pageError READ pageError NOTIFY pagingChanged)
  Q_PROPERTY(bool refreshRequired READ refreshRequired NOTIFY pagingChanged)
 public:
  enum Role { IdRole = Qt::UserRole + 1, TitleRole, UpdatedRole };
  explicit DialogModel(QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void setAll(QVector<DialogItem> rows);
  void setFilter(const QString& filter);
  QString filter() const { return filter_; }
  Q_INVOKABLE qint64 idAt(int row) const;
  Q_INVOKABLE QString titleAt(int row) const;
  Q_INVOKABLE int indexOfId(qint64 id) const;
  bool hasMore() const { return !page_token_.isEmpty(); }
  bool loading() const { return loading_ || search_timer_.isActive(); }
  QString pageError() const { return page_error_; }
  bool refreshRequired() const { return refresh_required_; }
  void refreshPages();
  Q_INVOKABLE void loadMore();
  void acceptPage(QVector<DialogItem> rows,QString next,quint64 generation,QString error={},bool changed=false,bool legacy=false);
 signals:
  void pagingChanged();
  void pageRequested(QString token,quint64 generation);

 private:
  void rebuild();
  QVector<DialogItem> all_;
  QVector<DialogItem> shown_;
  QString filter_;
  QString page_token_,page_error_;
  quint64 page_generation_=0;
  bool remote_=false,loading_=false,resetting_=false,refresh_required_=false;
  int target_count_=50;
  QTimer search_timer_;
};

class MessageModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int messageCount READ messageCount NOTIFY countChanged)
  Q_PROPERTY(bool historyOpen READ historyOpen WRITE setHistoryOpen NOTIFY historyOpenChanged)
  Q_PROPERTY(bool hasMore READ hasMore NOTIFY pagingChanged)
  Q_PROPERTY(bool loadingOlder READ loadingOlder NOTIFY pagingChanged)
  Q_PROPERTY(QString pageError READ pageError NOTIFY pagingChanged)
 public:
  enum Role { IdRole = Qt::UserRole + 1, SpeakerRole, BodyRole, TurnRole, StreamingRole };
  explicit MessageModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void setAll(QVector<MessageItem> rows);
  void add(MessageItem item);
  // Append a token delta to the live assistant bubble, creating it if needed.
  void pushToken(const QString& delta);
  void finishStream(const QString& finalText, qint64 turnId);
  void clear();
  bool hasMore() const { return next_before_ > 0; }
  bool loadingOlder() const { return loading_older_; }
  QString pageError() const { return page_error_; }
  quint64 pageGeneration() const { return page_generation_; }
  void resetPaging(qint64 next);
  void acceptOlder(QVector<MessageItem> rows, qint64 next, quint64 generation, QString error = {});
  Q_INVOKABLE void loadOlder();
  int messageCount() const { return rows_.size(); }
  bool historyOpen() const { return history_open_; }
  void setHistoryOpen(bool open);
  // Rows before the last 10 stay out of the view until history is opened.
  int origin() const {
    const int base = (!history_open_ && rows_.size() > 10) ? static_cast<int>(rows_.size() - 10) : 0;
    return base + extra_drop_;
  }
  Q_INVOKABLE int sourceRow(int visibleRow) const { return origin() + visibleRow; }
  Q_INVOKABLE QString bodyAt(int row) const {
    return row >= 0 && row < rows_.size() ? rows_.at(row).body : QString();
  }
  Q_INVOKABLE QString speakerAt(int row) const {
    return row >= 0 && row < rows_.size() ? rows_.at(row).speaker : QString();
  }

 signals:
  void countChanged();
  void historyOpenChanged();
  void pagingChanged();
  void olderRequested(qint64 before, quint64 generation);

 private:
  QVector<MessageItem> rows_;
  bool history_open_ = false;
  // While notifying a sliding window, pretend the dropped row is already gone.
  int extra_drop_ = 0;
  qint64 next_before_ = 0;
  quint64 page_generation_ = 0;
  bool loading_older_ = false;
  QString page_error_;
};

class TraceModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int stepCount READ stepCount NOTIFY statsChanged)
  Q_PROPERTY(int actionCount READ actionCount NOTIFY statsChanged)
  Q_PROPERTY(int thoughtCount READ thoughtCount NOTIFY statsChanged)
  Q_PROPERTY(int failureCount READ failureCount NOTIFY statsChanged)
  Q_PROPERTY(QString lastText READ lastText NOTIFY statsChanged)
 public:
  enum Role { KindRole = Qt::UserRole + 1, TextRole, ToolRole, ArgsRole, OkRole, DurationRole };
  explicit TraceModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void setAll(QVector<TraceItem> rows);
  void add(TraceItem item);
  // Merge SSE thought crumbs into the last thought row (avoids thousands of steps).
  void addOrMergeThought(const QString& text);
  void clear();
  TraceItem at(int row) const;
  Q_INVOKABLE QString kindAt(int row) const;
  Q_INVOKABLE QString toolAt(int row) const;
  Q_INVOKABLE QString argsAt(int row) const;
  int stepCount() const { return step_count_; }
  int actionCount() const { return action_count_; }
  int thoughtCount() const { return thought_count_; }
  int failureCount() const { return failure_count_; }
  QString lastText() const { return last_text_; }

 signals:
  void statsChanged();

 private:
  void recount();
  QVector<TraceItem> rows_;
  int step_count_ = 0;
  int action_count_ = 0;
  int thought_count_ = 0;
  int failure_count_ = 0;
  QString last_text_;
};

class TaskModel : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Role { IdRole = Qt::UserRole + 1, TurnRole, StatusRole, ErrorRole, CreatedRole };
  explicit TaskModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void setAll(QVector<TaskItem> rows);

 private:
  QVector<TaskItem> rows_;
};
