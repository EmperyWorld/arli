#include "redact.hpp"
#include "redact_store.hpp"
#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>
namespace arli::redact {
namespace {
struct Secrets {std::vector<std::string> values;size_t bytes=0;bool saturated=false;};
std::mutex mutex;
std::shared_ptr<const Secrets> known=std::make_shared<Secrets>();
std::shared_ptr<const Secrets> snapshot(){std::lock_guard lock(mutex);return known;}
std::string escaped(std::string_view input) {
  std::string out;constexpr char hex[]="0123456789abcdef";
  for(unsigned char c:input) {
    if(c=='"'||c=='\\'){out+='\\';out+=c;}
    else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";
    else if(c=='\b')out+="\\b";else if(c=='\f')out+="\\f";
    else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=c;
  }
  return out;
}
std::string filter(std::string_view input,bool streaming,size_t& consumed,bool finishing=false) {
  const auto secrets=snapshot();std::string out;consumed=0;
  if(secrets->saturated){consumed=input.size();return {};}
  std::vector<store::Fingerprint> fingerprints;
  try { fingerprints=store::records(); } catch(...) {consumed=input.size();return {};}
  std::sort(fingerprints.begin(),fingerprints.end(),[](const auto& a,const auto& b){return a.size>b.size;});
  size_t maximum=0;for(const auto& record:fingerprints)maximum=std::max(maximum,record.size);
  std::vector<std::uint64_t> hashes(input.size()+1),powers(std::min(maximum,input.size())+1,1);
  for(size_t i=0;i<input.size();++i)hashes[i+1]=hashes[i]*131+(static_cast<unsigned char>(input[i])+1);
  for(size_t i=1;i<powers.size();++i)powers[i]=powers[i-1]*131;
  std::string mask="[redacted]";
  for(const auto& secret:secrets->values)if(mask.find(secret)!=std::string::npos)mask.clear();
  for(const auto& record:fingerprints)for(size_t p=0;p+record.size<=mask.size();++p)
    if(store::fingerprint(std::string_view(mask).substr(p,record.size)).digest==record.digest){mask.clear();break;}
  while(consumed<input.size()) {
    size_t match=0;bool prefix=false;const auto tail=input.substr(consumed);
    if(streaming&&tail.size()<maximum)break;
    for(const auto& secret:secrets->values) {
      if(tail.size()>=secret.size()&&tail.substr(0,secret.size())==secret){match=secret.size();break;}
      if((streaming||finishing)&&tail.size()<secret.size()&&secret.compare(0,tail.size(),tail)==0)prefix=true;
    }
    if(!match)for(const auto& record:fingerprints) {
      if(tail.size()<record.size)continue;
      if(hashes[consumed+record.size]-hashes[consumed]*powers[record.size]!=record.rolling)continue;
      if(store::fingerprint(tail.substr(0,record.size)).digest==record.digest){match=record.size;break;}
    }
    if(prefix&&match==0){if(finishing){out+=mask;consumed=input.size();}break;}
    if(match){out+=mask;consumed+=match;}
    else {
      const auto lead=static_cast<unsigned char>(input[consumed]);
      const size_t width=lead<0x80?1:lead>=0xc2&&lead<=0xdf?2:lead>=0xe0&&lead<=0xef?3:lead>=0xf0&&lead<=0xf4?4:0;
      if(streaming&&width&&tail.size()<width)break;
      bool valid=width&&tail.size()>=width;
      for(size_t i=1;valid&&i<width;++i)valid=(static_cast<unsigned char>(tail[i])&0xc0)==0x80;
      if(valid&&width>=3){const auto second=static_cast<unsigned char>(tail[1]);valid=!(lead==0xe0&&second<0xa0)&&!(lead==0xed&&second>=0xa0)&&!(lead==0xf0&&second<0x90)&&!(lead==0xf4&&second>=0x90);}
      if(valid){out.append(tail.substr(0,width));consumed+=width;}
      else {out+="\xef\xbf\xbd";++consumed;}
    }
  }
  return out;
}
}
void remember(std::string_view secret) {
  if(secret.empty())return;
  std::lock_guard lock(mutex);if(known->saturated)return;
  auto next=std::make_shared<Secrets>(*known);
  for(const auto& value:{std::string(secret),escaped(secret)}) {
    if(std::find(next->values.begin(),next->values.end(),value)!=next->values.end())continue;
    if(value.size()>65536||next->values.size()>=512||next->bytes+value.size()>1024*1024){next->saturated=true;break;}
    store::append(value);
    next->values.push_back(value);next->bytes+=value.size();
  }
  std::sort(next->values.begin(),next->values.end(),[](const auto& a,const auto& b){return a.size()>b.size();});known=std::move(next);
}
void enable_persistence(const std::filesystem::path& path) {
  std::lock_guard lock(mutex);store::enable(path);
  for(const auto& value:known->values)store::append(value);
}
void ensure_available(){(void)store::records();}
std::string text(std::string_view input){size_t used=0;return filter(input,false,used);}
std::string Stream::push(std::string_view chunk){pending_+=chunk;size_t used=0;auto out=filter(pending_,true,used);pending_.erase(0,used);return out;}
std::string Stream::finish(){size_t used=0;auto out=filter(pending_,false,used,true);pending_.clear();return out;}
void message(google::protobuf::Message& value) {
  const auto* reflection=value.GetReflection();std::vector<const google::protobuf::FieldDescriptor*> fields;
  reflection->ListFields(value,&fields);
  for(const auto* field:fields) {
    const int count=field->is_repeated()?reflection->FieldSize(value,field):1;
    for(int i=0;i<count;++i) {
      if(field->cpp_type()==google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        message(*(field->is_repeated()?reflection->MutableRepeatedMessage(&value,field,i):reflection->MutableMessage(&value,field)));
      } else if(field->cpp_type()==google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
        auto raw=field->is_repeated()?reflection->GetRepeatedString(value,field,i):reflection->GetString(value,field);
        auto safe=text(raw);if(safe==raw)continue;
        if(field->is_repeated())reflection->SetRepeatedString(&value,field,i,safe);else reflection->SetString(&value,field,safe);
      }
    }
  }
}
}
