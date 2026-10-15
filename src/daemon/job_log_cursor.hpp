#pragma once
#include "job_log_reader.hpp"
#include "secrets.hpp"
#include <openssl/hmac.h>
#include <stdexcept>
namespace arli {
inline std::string job_cursor_signature(const std::string& body,const std::string& key){
  unsigned char digest[EVP_MAX_MD_SIZE];unsigned size=0;
  if(key.empty()||!HMAC(EVP_sha256(),key.data(),static_cast<int>(key.size()),
      reinterpret_cast<const unsigned char*>(body.data()),body.size(),digest,&size))throw std::runtime_error("job cursor key unavailable");
  static constexpr char hex[]="0123456789abcdef";std::string out;
  for(unsigned i=0;i<size;++i){out+=hex[digest[i]>>4];out+=hex[digest[i]&15];}return out;
}
inline std::string encode_job_cursor(std::int64_t id,const JobLogPosition& position,const std::string& key){
  if(position.generation.empty())return {};
  const auto body="j1|"+std::to_string(id)+"|"+position.encode();return body+"|"+job_cursor_signature(body,key);
}
inline JobLogPosition decode_job_cursor(std::int64_t id,const std::string& cursor,const std::string& key){
  if(cursor.empty())return {};
  const auto prefix="j1|"+std::to_string(id)+"|";const auto split=cursor.rfind('|');
  if(cursor.size()>280||cursor.rfind(prefix,0)!=0||split==std::string::npos||split<prefix.size()||
     !secrets::constant_time_equal(cursor.substr(split+1),job_cursor_signature(cursor.substr(0,split),key)))
    throw std::invalid_argument("invalid job log position");
  return JobLogPosition::parse(cursor.substr(prefix.size(),split-prefix.size()));
}
}
