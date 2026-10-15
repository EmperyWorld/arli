#include "models.hpp"

void MessageModel::resetPaging(qint64 next) {
  ++page_generation_;next_before_=next;loading_older_=false;page_error_.clear();emit pagingChanged();
}
void MessageModel::loadOlder() {
  if(!hasMore()||loading_older_)return;
  loading_older_=true;page_error_.clear();emit pagingChanged();
  emit olderRequested(next_before_,page_generation_);
}
void MessageModel::acceptOlder(QVector<MessageItem> rows,qint64 next,quint64 generation,QString error) {
  if(generation!=page_generation_||!loading_older_)return;
  loading_older_=false;page_error_=std::move(error);
  if(page_error_.isEmpty()) {
    // Validate monotonic boundaries before modifying the visible conversation.
    qint64 previous=0;
    for(const auto& row:rows){
      if(row.id<=previous||row.id>=next_before_){page_error_=QStringLiteral("Нарушен порядок истории");break;}
      previous=row.id;
    }
    if(next<0||(next>0&&(rows.isEmpty()||next!=rows.front().id||next>=next_before_)))
      page_error_=QStringLiteral("Некорректная граница страницы");
  }
  if(page_error_.isEmpty()) {
    setHistoryOpen(true);
    if(!rows.isEmpty()) {
      beginInsertRows({},0,static_cast<int>(rows.size())-1);
      rows_ = std::move(rows)+rows_;endInsertRows();emit countChanged();
    }
    next_before_=next;
  }
  emit pagingChanged();
}
