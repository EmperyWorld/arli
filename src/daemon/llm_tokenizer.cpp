#include "llm_tokenizer.hpp"
#include <curl/curl.h>

namespace arli::llm_internal {
namespace {
size_t collect(char* data,size_t size,size_t count,void* opaque){
  auto& out=*static_cast<std::string*>(opaque);const auto bytes=size*count;
  if(out.size()+bytes>8*1024*1024)return 0;out.append(data,bytes);return bytes;
}
std::optional<std::string> local_origin(const std::string& value){
  CURLU* url=curl_url();if(!url)return std::nullopt;
  struct Cleanup{CURLU* url;~Cleanup(){curl_url_cleanup(url);}} cleanup{url};
  if(curl_url_set(url,CURLUPART_URL,value.c_str(),0)!=CURLUE_OK)return std::nullopt;
  auto part=[&](CURLUPart which,unsigned flags=0){char* raw=nullptr;std::string result;
    if(curl_url_get(url,which,&raw,flags)==CURLUE_OK)result=raw;curl_free(raw);return result;};
  const auto scheme=part(CURLUPART_SCHEME),host=part(CURLUPART_HOST),port=part(CURLUPART_PORT,CURLU_DEFAULT_PORT);
  if(scheme!="http"||(host!="127.0.0.1"&&host!="localhost"&&host!="[::1]"&&host!="::1"))return std::nullopt;
  return scheme+"://"+(host.find(':')!=std::string::npos&&host.front()!='['?"["+host+"]":host)+":"+port;
}
std::optional<nlohmann::json> post(const std::string& url,const nlohmann::json& body,const std::string& key){
  CURL* curl=curl_easy_init();if(!curl)return std::nullopt;
  struct curl_slist* headers=curl_slist_append(nullptr,"Content-Type: application/json");
  if(!key.empty())headers=curl_slist_append(headers,("Authorization: Bearer "+key).c_str());
  const auto wire=body.dump();std::string response;
  curl_easy_setopt(curl,CURLOPT_URL,url.c_str());curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
  curl_easy_setopt(curl,CURLOPT_POSTFIELDS,wire.c_str());curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,wire.size());
  curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,collect);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response);
  curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,750L);curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,2000L);
  curl_easy_setopt(curl,CURLOPT_PROXY,"");const auto rc=curl_easy_perform(curl);long status=0;
  curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);curl_easy_cleanup(curl);curl_slist_free_all(headers);
  if(rc!=CURLE_OK||status<200||status>=300)return std::nullopt;
  auto parsed=nlohmann::json::parse(response,nullptr,false);if(parsed.is_discarded())return std::nullopt;return parsed;
}
}

std::optional<int> exact_prompt_tokens(const LlmConfig& cfg,const nlohmann::json& messages,
                                       const nlohmann::json& tools){
  const auto origin=local_origin(cfg.base_url);if(!origin)return std::nullopt;
  const auto rendered=post(*origin+"/apply-template",{{"messages",messages},{"tools",tools},
      {"add_generation_prompt",true}},cfg.api_key);
  if(!rendered||!rendered->contains("prompt")||!(*rendered)["prompt"].is_string())return std::nullopt;
  const auto tokenized=post(*origin+"/tokenize",{{"content",(*rendered)["prompt"]},{"add_special",false}},cfg.api_key);
  if(!tokenized||!tokenized->contains("tokens")||!(*tokenized)["tokens"].is_array())return std::nullopt;
  return static_cast<int>((*tokenized)["tokens"].size());
}
}
