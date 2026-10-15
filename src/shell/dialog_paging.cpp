#include "models.hpp"
#include <QSet>

DialogModel::DialogModel(QObject* parent):QAbstractListModel(parent) {
  search_timer_.setSingleShot(true);search_timer_.setInterval(180);
  connect(&search_timer_,&QTimer::timeout,this,&DialogModel::refreshPages);
}
void DialogModel::refreshPages() {
  search_timer_.stop();++page_generation_;remote_=true;loading_=true;resetting_=true;
  refresh_required_=false;page_token_.clear();page_error_.clear();target_count_=50;
  emit pagingChanged();emit pageRequested({},page_generation_);
}
void DialogModel::loadMore() {
  if(loading())return;
  if(refresh_required_||resetting_){refreshPages();return;}
  if(!hasMore())return;
  loading_=true;page_error_.clear();target_count_=all_.size()+50;
  emit pagingChanged();emit pageRequested(page_token_,page_generation_);
}
void DialogModel::acceptPage(QVector<DialogItem> rows,QString next,quint64 generation,QString error,bool changed,bool legacy) {
  if(generation!=page_generation_||!loading_)return;
  loading_=false;page_error_=std::move(error);refresh_required_=changed;
  if(!page_error_.isEmpty()){emit pagingChanged();return;}
  if(!next.isEmpty()&&next==page_token_){page_error_=QStringLiteral("Повторена граница страницы");emit pagingChanged();return;}
  const bool reset=resetting_;
  if(resetting_)all_.clear();resetting_=false;remote_=!legacy;
  QSet<qint64> ids;for(const auto& row:all_)ids.insert(row.id);
  const auto needle=filter_.trimmed();
  for(auto& row:rows)if(!ids.contains(row.id)&&(legacy||needle.isEmpty()||row.title.contains(needle,Qt::CaseInsensitive))){
    ids.insert(row.id);all_.push_back(std::move(row));
  }
  page_token_=std::move(next);
  if(reset||legacy)rebuild();
  else if(all_.size()>shown_.size()){
    const int first=shown_.size();beginInsertRows({},first,all_.size()-1);
    for(int i=first;i<all_.size();++i)shown_.push_back(all_[i]);endInsertRows();
  }
  // Only matching rows are retained. A Unicode search can traverse every server
  // page without accumulating nonmatching titles or issuing parallel requests.
  if(!legacy&&hasMore()&&all_.size()<target_count_){loading_=true;emit pageRequested(page_token_,page_generation_);}
  emit pagingChanged();
}
