#include "models.hpp"

int DialogModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : shown_.size();
}

QVariant DialogModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= shown_.size()) return {};
  const auto& row = shown_.at(index.row());
  switch (role) {
    case IdRole: return row.id;
    case TitleRole: return row.title;
    case UpdatedRole: return row.updatedAt;
    default: return {};
  }
}

QHash<int, QByteArray> DialogModel::roleNames() const {
  return {{IdRole, "dialogId"}, {TitleRole, "title"}, {UpdatedRole, "updatedAt"}};
}

void DialogModel::setAll(QVector<DialogItem> rows) {
  search_timer_.stop();++page_generation_;remote_=false;loading_=false;page_token_.clear();page_error_.clear();
  resetting_=false;refresh_required_=false;
  all_ = std::move(rows);
  rebuild();
  emit pagingChanged();
}

qint64 DialogModel::idAt(int row) const {
  return row >= 0 && row < shown_.size() ? shown_.at(row).id : 0;
}

QString DialogModel::titleAt(int row) const {
  return row >= 0 && row < shown_.size() ? shown_.at(row).title : QString();
}

int DialogModel::indexOfId(qint64 id) const {
  for (int i = 0; i < shown_.size(); ++i) {
    if (shown_.at(i).id == id) return i;
  }
  return -1;
}

void DialogModel::setFilter(const QString& filter) {
  if (filter_ == filter) return;
  filter_ = filter;
  rebuild();
  if(remote_){++page_generation_;loading_=false;search_timer_.start();emit pagingChanged();}
}

void DialogModel::rebuild() {
  beginResetModel();
  shown_.clear();
  const QString needle = filter_.trimmed();
  for (const auto& row : all_) {
    if (needle.isEmpty() || row.title.contains(needle, Qt::CaseInsensitive)) shown_.push_back(row);
  }
  endResetModel();
}

int MessageModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size()) - origin();
}

void MessageModel::setHistoryOpen(bool open) {
  if (history_open_ == open) return;
  history_open_ = open;
  beginResetModel();
  endResetModel();
  emit historyOpenChanged();
}

QVariant MessageModel::data(const QModelIndex& index, int role) const {
  const int src = origin() + index.row();
  if (!index.isValid() || src < 0 || src >= rows_.size()) return {};
  const auto& row = rows_.at(src);
  switch (role) {
    case IdRole: return row.id;
    case SpeakerRole: return row.speaker;
    case BodyRole: return row.body;
    case TurnRole: return row.turnId;
    case StreamingRole: return row.streaming;
    default: return {};
  }
}

QHash<int, QByteArray> MessageModel::roleNames() const {
  return {{IdRole, "messageId"}, {SpeakerRole, "speaker"}, {BodyRole, "body"},
          {TurnRole, "turnId"}, {StreamingRole, "streaming"}};
}

void MessageModel::setAll(QVector<MessageItem> rows) {
  resetPaging(0);
  beginResetModel();
  rows_ = std::move(rows);
  history_open_ = false;
  endResetModel();
  emit countChanged();
  emit historyOpenChanged();
}

void MessageModel::add(MessageItem item) {
  const bool slide = !history_open_ && rows_.size() >= 10;
  if (!slide) {
    const int at = static_cast<int>(rows_.size()) - origin();
    beginInsertRows({}, at, at);
    rows_.push_back(std::move(item));
    endInsertRows();
  } else {
    extra_drop_ = 1;
    beginRemoveRows({}, 0, 0);
    endRemoveRows();
    rows_.push_back(std::move(item));
    extra_drop_ = 0;
    const int at = static_cast<int>(rows_.size()) - origin() - 1;
    beginInsertRows({}, at, at);
    endInsertRows();
  }
  emit countChanged();
}

void MessageModel::pushToken(const QString& delta) {
  if (delta.isEmpty()) return;
  if (!rows_.isEmpty() && rows_.back().streaming) {
    rows_.back().body += delta;
    emit dataChanged(index(static_cast<int>(rows_.size()) - 1 - origin()), index(static_cast<int>(rows_.size()) - 1 - origin()),
                     {BodyRole});
    return;
  }
  add(MessageItem{0, QStringLiteral("assistant"), delta, 0, true});
}

void MessageModel::finishStream(const QString& finalText, qint64 turnId) {
  if (!rows_.isEmpty() && rows_.back().streaming) {
    rows_.back().body = finalText;
    rows_.back().turnId = turnId;
    rows_.back().streaming = false;
    const QModelIndex idx = index(static_cast<int>(rows_.size()) - 1 - origin());
    emit dataChanged(idx, idx, {BodyRole, TurnRole, StreamingRole});
    return;
  }
  if (!finalText.isEmpty()) add(MessageItem{0, QStringLiteral("assistant"), finalText, turnId, false});
}

void MessageModel::clear() {
  resetPaging(0);
  beginResetModel();
  rows_.clear();
  history_open_ = false;
  endResetModel();
  emit countChanged();
  emit historyOpenChanged();
}

void TraceModel::recount() {
  step_count_ = rows_.size();
  action_count_ = thought_count_ = failure_count_ = 0;
  last_text_.clear();
  for (const auto& row : rows_) {
    if (row.kind == QLatin1String("thought")) ++thought_count_;
    else if (row.kind == QLatin1String("action")) ++action_count_;
    else if (row.kind == QLatin1String("obs_fail") || row.kind == QLatin1String("error")) ++failure_count_;
    const QString line = row.text.isEmpty() ? row.tool : row.text;
    if (!line.isEmpty()) last_text_ = line;
  }
  emit statsChanged();
}

int TraceModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : rows_.size();
}

QVariant TraceModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= rows_.size()) return {};
  const auto& row = rows_.at(index.row());
  switch (role) {
    case KindRole: return row.kind;
    case TextRole: return row.text;
    case ToolRole: return row.tool;
    case ArgsRole: return row.args;
    case OkRole: return row.ok;
    case DurationRole: return row.durationMs;
    default: return {};
  }
}

QHash<int, QByteArray> TraceModel::roleNames() const {
  return {{KindRole, "kind"}, {TextRole, "text"}, {ToolRole, "tool"},
          {ArgsRole, "args"}, {OkRole, "ok"}, {DurationRole, "durationMs"}};
}

void TraceModel::setAll(QVector<TraceItem> rows) {
  beginResetModel();
  rows_ = std::move(rows);
  endResetModel();
  recount();
}

void TraceModel::add(TraceItem item) {
  beginInsertRows({}, rows_.size(), rows_.size());
  rows_.push_back(std::move(item));
  endInsertRows();
  recount();
}

void TraceModel::addOrMergeThought(const QString& text) {
  if (text.isEmpty()) return;
  constexpr int kCrumb = 64;
  if (!rows_.isEmpty() && rows_.back().kind == QLatin1String("thought") &&
      (text.size() < kCrumb || rows_.back().text.size() < kCrumb * 4)) {
    rows_.back().text = text.size() < kCrumb ? (rows_.back().text + text) : text;
    emit dataChanged(index(rows_.size() - 1), index(rows_.size() - 1), {TextRole});
    last_text_ = rows_.back().text;
    emit statsChanged();
    return;
  }
  add(TraceItem{QStringLiteral("thought"), text, {}, {}, {}, true, 0});
}

void TraceModel::clear() {
  beginResetModel();
  rows_.clear();
  endResetModel();
  recount();
}

QString TraceModel::kindAt(int row) const {
  return row >= 0 && row < rows_.size() ? rows_.at(row).kind : QString();
}

QString TraceModel::toolAt(int row) const {
  return row >= 0 && row < rows_.size() ? rows_.at(row).tool : QString();
}

QString TraceModel::argsAt(int row) const {
  return row >= 0 && row < rows_.size() ? rows_.at(row).args : QString();
}

TraceItem TraceModel::at(int row) const {
  return (row < 0 || row >= rows_.size()) ? TraceItem{} : rows_.at(row);
}

int TaskModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : rows_.size();
}

QVariant TaskModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= rows_.size()) return {};
  const auto& row = rows_.at(index.row());
  switch (role) {
    case IdRole: return row.id;
    case TurnRole: return row.turnId;
    case StatusRole: return row.status;
    case ErrorRole: return row.error;
    case CreatedRole: return row.createdAt;
    default: return {};
  }
}

QHash<int, QByteArray> TaskModel::roleNames() const {
  return {{IdRole, "taskId"}, {TurnRole, "turnId"}, {StatusRole, "status"},
          {ErrorRole, "errorText"}, {CreatedRole, "createdAt"}};
}

void TaskModel::setAll(QVector<TaskItem> rows) {
  beginResetModel();
  rows_ = std::move(rows);
  endResetModel();
}
