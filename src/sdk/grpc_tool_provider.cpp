#include "grpc_tool_provider.hpp"
#include "grpc_tool_schema.hpp"
#include "auth.hpp"
#include "redact.hpp"
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/util/json_util.h>
#include <grpcpp/generic/generic_stub.h>
#include <map>
#include <set>
#include <stdexcept>
namespace arli {
namespace {
constexpr size_t kMaxBytes=512*1024;
bool endpoint_ok(const std::string& endpoint) {
  size_t pos=endpoint.rfind("127.0.0.1:",0)==0?10:endpoint.rfind("[::1]:",0)==0?6:0;
  if(!pos || pos==endpoint.size() || endpoint.size()-pos>5)return false;
  int port=0;for(;pos<endpoint.size();++pos){const char c=endpoint[pos];if(c<'0'||c>'9')return false;port=port*10+c-'0';}
  return port>0&&port<=65535;
}
std::string redact_credential(std::string text,const std::string& token) {
  for(size_t p=0;!token.empty()&&(p=text.find(token,p))!=std::string::npos;p+=10)text.replace(p,token.size(),"[redacted]");
  return text;
}
}
struct GrpcToolProvider::Impl {
  struct Entry {
    GrpcToolMethod spec;
    google::protobuf::DescriptorPool pool;
    google::protobuf::DynamicMessageFactory factory{&pool};
    const google::protobuf::MethodDescriptor* method=nullptr;
    grpc_tools_detail::json schema;
  };
  std::map<std::string,std::unique_ptr<Entry>> entries;
};
GrpcToolProvider::GrpcToolProvider():impl_(std::make_unique<Impl>()){}
GrpcToolProvider::~GrpcToolProvider()=default;
void GrpcToolProvider::add(GrpcToolMethod spec) {
  if(impl_->entries.size()>=32 || spec.tool_name.rfind("grpc_",0)!=0 || spec.tool_name.size()<6 || spec.tool_name.size()>64 ||
      !endpoint_ok(spec.endpoint) || spec.deadline_ms<1 || spec.deadline_ms>60000 || spec.bearer_token.empty() ||
      spec.bearer_token.size()>8192 || spec.descriptor_set.size()>4*1024*1024 || contains(spec.tool_name))
    throw std::invalid_argument("invalid or duplicate gRPC tool configuration");
  for(char c:spec.tool_name)if(!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='_')throw std::invalid_argument("invalid gRPC tool name");
  for(unsigned char c:spec.bearer_token)
    if(!(c>='a'&&c<='z')&&!(c>='A'&&c<='Z')&&!(c>='0'&&c<='9')&&std::string("-._~+/=").find(c)==std::string::npos)
      throw std::invalid_argument("service credential must use bearer token68 characters");
  arli::redact::remember(spec.bearer_token);
  auto entry=std::make_unique<Impl::Entry>();entry->spec=std::move(spec);
  google::protobuf::FileDescriptorSet files;
  if(!files.ParseFromString(entry->spec.descriptor_set) || files.file_size()>128)throw std::invalid_argument("invalid descriptor set");
  std::set<std::string> loaded;
  for(int pass=0;pass<files.file_size();++pass) {
    bool progress=false;
    for(const auto& file:files.file()) {
      if(loaded.count(file.name()))continue;
      bool ready=true;for(const auto& dep:file.dependency())ready&=loaded.count(dep)>0;
      if(!ready)continue;
      if(!entry->pool.BuildFile(file))throw std::invalid_argument("invalid protobuf descriptors");
      loaded.insert(file.name());progress=true;
    }
    if(!progress)break;
  }
  if(loaded.size()!=static_cast<size_t>(files.file_size()))throw std::invalid_argument("descriptor imports missing or duplicate");
  entry->method=entry->pool.FindMethodByName(entry->spec.method);
  if(!entry->method || entry->method->client_streaming() || entry->method->server_streaming())throw std::invalid_argument("only configured unary methods are supported");
  size_t schema_budget=2048;
  entry->schema={{"type","function"},{"function",{{"name",entry->spec.tool_name},{"description",entry->spec.method+(entry->spec.mutating?" (requires operator approval)":" (read only)")},
      {"parameters",grpc_tools_detail::message_schema(entry->method->input_type(),0,schema_budget)}}}};
  impl_->entries.emplace(entry->spec.tool_name,std::move(entry));
}
bool GrpcToolProvider::contains(const std::string& name)const{return impl_->entries.count(name)>0;}
bool GrpcToolProvider::mutating(const std::string& name)const{const auto i=impl_->entries.find(name);return i!=impl_->entries.end()&&i->second->spec.mutating;}
std::string GrpcToolProvider::schemas_json()const{
  auto result=grpc_tools_detail::json::array();for(const auto& [_,entry]:impl_->entries)result.push_back(entry->schema);return result.dump();
}
GrpcToolResult GrpcToolProvider::invoke(const std::string& name,const std::string& arguments,bool read_only,
    std::atomic<bool>* cancel,const std::function<bool(const std::string&)>& approve)const {
  const auto found=impl_->entries.find(name);
  if(found==impl_->entries.end())return {false,"gRPC method is not allowlisted"};
  auto& entry=*found->second;const auto& spec=entry.spec;
  if(read_only&&spec.mutating)return {false,"read_only: gRPC mutation disabled"};
  if(arguments.size()>kMaxBytes)return {false,"gRPC arguments exceed limit"};
  if(cancel&&cancel->load())return {false,"gRPC cancelled before dispatch"};
  std::unique_ptr<google::protobuf::Message> request(entry.factory.GetPrototype(entry.method->input_type())->New());
  if(!google::protobuf::util::JsonStringToMessage(arguments,request.get()).ok() || !request->IsInitialized())return {false,"gRPC arguments do not match protobuf schema"};
  std::string canonical;
  if(!google::protobuf::util::MessageToJsonString(*request,&canonical).ok())return {false,"gRPC arguments cannot be encoded"};
  if(spec.mutating&&(!approve||!approve(name+" "+redact_credential(canonical,spec.bearer_token))))return {false,"gRPC mutation rejected or cancelled by operator"};
  if(cancel&&cancel->load())return {false,"gRPC cancelled before dispatch"};
  const auto bytes=request->SerializeAsString();if(bytes.size()>kMaxBytes)return {false,"gRPC request exceeds limit"};
  grpc::Slice slice(bytes);grpc::ByteBuffer input(&slice,1),output;
  grpc::ClientContext context;auth::bearer(context,spec.bearer_token);
  context.set_deadline(std::chrono::system_clock::now()+std::chrono::milliseconds(spec.deadline_ms));
  grpc::GenericStub stub(auth::make_bounded_channel(spec.endpoint,kMaxBytes));grpc::CompletionQueue queue;grpc::Status status;
  auto call=stub.PrepareUnaryCall(&context,"/"+entry.method->service()->full_name()+"/"+entry.method->name(),input,&queue);
  if(!call)return {false,"gRPC call preparation failed"};
  call->StartCall();int tag=0;call->Finish(&output,&status,&tag);
  void* received=nullptr;bool ok=false,completed=false;
  for(;;){
    if(cancel&&cancel->load())context.TryCancel();
    const auto next=queue.AsyncNext(&received,&ok,std::chrono::system_clock::now()+std::chrono::milliseconds(20));
    if(next==grpc::CompletionQueue::GOT_EVENT){completed=received==&tag&&ok;break;}
    if(next==grpc::CompletionQueue::SHUTDOWN)break;
  }
  queue.Shutdown();while(queue.Next(&received,&ok)){}
  if(!completed)return {false,"gRPC completion failed; effect may be unknown"};
  if(!status.ok())return {false,"gRPC error "+std::to_string(status.error_code())+(spec.mutating?": эффект может быть неизвестен; не повторяй автоматически, проверь состояние.":"")};
  std::vector<grpc::Slice> slices;if(!output.Dump(&slices).ok())return {false,"invalid gRPC response buffer; effect may be unknown"};
  std::string response;for(const auto& part:slices){if(response.size()+part.size()>kMaxBytes)return {false,"gRPC response exceeds limit; effect may be unknown"};response.append(reinterpret_cast<const char*>(part.begin()),part.size());}
  std::unique_ptr<google::protobuf::Message> message(entry.factory.GetPrototype(entry.method->output_type())->New());
  if(!message->ParseFromString(response))return {false,"invalid protobuf response; effect may be unknown"};
  std::string json;if(!google::protobuf::util::MessageToJsonString(*message,&json).ok()||json.size()>kMaxBytes)return {false,"gRPC JSON response exceeds limit or is invalid; effect may be unknown"};
  return {true,redact_credential(json,spec.bearer_token)};
}
}
