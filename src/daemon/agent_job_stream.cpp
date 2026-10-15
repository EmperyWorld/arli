#include "agent_service.hpp"
#include "job_log_reader.hpp"
#include "job_log_cursor.hpp"
#include "jobs.hpp"
#include "redact.hpp"
#include <deque>
#include <thread>
namespace arli {
namespace {
struct Span {JobLogPosition start;size_t bytes;};
void consume(std::deque<Span>& spans,size_t bytes,JobLogPosition& committed){
  while(bytes&&!spans.empty()){
    auto& span=spans.front();const auto count=std::min(bytes,span.bytes);
    span.start.offset+=count;span.bytes-=count;bytes-=count;committed=span.start;
    if(!span.bytes)spans.pop_front();
  }
}
}
grpc::Status AgentService::WatchJobLog(grpc::ServerContext* ctx,const v1::WatchJobLogRequest* req,
                                      grpc::ServerWriter<v1::JobLogChunk>* writer) try {
  using Code=grpc::StatusCode;
  if(req->id()<=0||req->expected_dialog_id()<0||req->expected_task_id()<0)return {Code::INVALID_ARGUMENT,"invalid job request"};
  const auto key=secrets::load(secrets::internal_token_path());
  auto position=decode_job_cursor(req->id(),req->after_position(),key);
  auto stored=db_->get_job(req->id());if(!stored)return {Code::NOT_FOUND,"job not found"};
  if((req->expected_dialog_id()&&stored->dialog_id!=req->expected_dialog_id())||
     (req->expected_task_id()&&stored->task_id!=req->expected_task_id()))return {Code::FAILED_PRECONDITION,"job ownership mismatch"};
  if(job_log_streams_.fetch_add(1)>=8){job_log_streams_.fetch_sub(1);return {Code::RESOURCE_EXHAUSTED,"job stream limit"};}
  struct Release {std::atomic<int>& count;~Release(){count.fetch_sub(1);}} release{job_log_streams_};
  // Also bound synchronous Write for clients that omit deadlines or stop reading.
  std::jthread expiry([ctx](std::stop_token stop){
    for(int n=0;n<150&&!stop.stop_requested();++n)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if(!stop.stop_requested())ctx->TryCancel();
  });
  auto committed=position;redact::Stream filter;std::deque<Span> spans;bool first=true;
  while(!ctx->IsCancelled()){
    stored=db_->get_job(req->id());if(!stored)return {Code::NOT_FOUND,"job history expired"};
    const auto job=refresh_job(*db_,*stored);redact::ensure_available();
    const auto slice=read_job_log(job.log_path,position);
    if(slice.gap){filter=redact::Stream{};spans.clear();committed=slice.start;}
    if(spans.empty()&&!filter.buffered_bytes())committed=slice.start;
    const auto buffered=filter.buffered_bytes();
    if(!slice.text.empty()){
      if(!spans.empty()&&spans.back().start.generation==slice.start.generation&&
         spans.back().start.offset+spans.back().bytes==slice.start.offset)spans.back().bytes+=slice.text.size();
      else spans.push_back({slice.start,slice.text.size()});
    }
    auto text=filter.push(slice.text);consume(spans,buffered+slice.text.size()-filter.buffered_bytes(),committed);
    position=slice.next;const bool terminal=job.status!="running"&&slice.end;
    if(terminal){const auto pending=filter.buffered_bytes();text+=filter.finish();consume(spans,pending,committed);}
    if(text.size()>1024*1024)throw std::runtime_error("job stream chunk limit");
    if(first||slice.gap||terminal||!text.empty()){
      v1::JobLogChunk chunk;chunk.set_id(job.id);chunk.set_text(std::move(text));chunk.set_next_position(encode_job_cursor(job.id,committed,key));
      chunk.set_gap(slice.gap);chunk.set_terminal(terminal);chunk.set_status(job.status);
      chunk.set_exit_code(job.exit_code);chunk.set_exit_code_known(job.exit_code>=0);
      if(!writer->Write(chunk))return grpc::Status::CANCELLED;first=false;
    }
    if(terminal)return grpc::Status::OK;
    if(slice.end)std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return grpc::Status::CANCELLED;
}catch(const JobLogBusy&){return {grpc::StatusCode::UNAVAILABLE,"job log rotation in progress"};}
catch(const std::invalid_argument&){return {grpc::StatusCode::INVALID_ARGUMENT,"invalid job log position"};}
catch(const std::exception&){return {grpc::StatusCode::FAILED_PRECONDITION,"job log unavailable or filesystem lacks stable birth time"};}
}
