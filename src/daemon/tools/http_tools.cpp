#include "http_tools.hpp"
#include "public_address.hpp"

#include <curl/curl.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <cctype>
#include <algorithm>
#include <sstream>

namespace arli {
bool pin_public_addresses(const std::string& host, const std::string& port,
                          const std::vector<std::string>& addresses,
                          std::string& pinned, std::string& why) {
  std::string resolved;std::vector<std::string> unique;
  for(const auto& text:addresses){
    in_addr v4{};in6_addr v6{};const bool ipv4=inet_pton(AF_INET,text.c_str(),&v4)==1;
    const bool ipv6=!ipv4&&inet_pton(AF_INET6,text.c_str(),&v6)==1;
    if((!ipv4&&!ipv6)||!public_address(text)){why="private/non-public address blocked";return false;}
    if(std::find(unique.begin(),unique.end(),text)!=unique.end())continue;unique.push_back(text);
    if(!resolved.empty())resolved+=',';resolved+=ipv6?"["+text+"]":text;
  }
  if(resolved.empty()){why="cannot resolve host";return false;}
  in_addr literal4{};in6_addr literal6{};
  if(inet_pton(AF_INET,host.c_str(),&literal4)!=1&&inet_pton(AF_INET6,host.c_str(),&literal6)!=1)
    pinned=host+":"+port+":"+resolved;
  return true;
}
namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  const size_t n = size * nmemb;
  if (out->size() + n > 1024 * 1024) {
    return 0;
  }
  out->append(ptr, n);
  return n;
}

bool resolve_public_url(const std::string& url, std::string& pinned, std::string& why) {
  CURLU* parsed = curl_url();
  if (!parsed) { why = "URL parser unavailable"; return false; }
  struct Cleanup { CURLU* p; ~Cleanup() { curl_url_cleanup(p); } } cleanup{parsed};
  if (curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0) != CURLUE_OK) {
    why = "bad url"; return false;
  }
  auto part = [&](CURLUPart kind, unsigned flags = 0) {
    char* raw = nullptr; std::string value;
    if (curl_url_get(parsed, kind, &raw, flags) == CURLUE_OK) value = raw;
    curl_free(raw); return value;
  };
  const auto scheme = part(CURLUPART_SCHEME);
  if (scheme != "http" && scheme != "https") { why = "only http/https"; return false; }
  if (!part(CURLUPART_USER).empty() || !part(CURLUPART_PASSWORD).empty()) {
    why = "URL credentials forbidden"; return false;
  }
  auto host = part(CURLUPART_HOST);
  const auto port = part(CURLUPART_PORT, CURLU_DEFAULT_PORT);
  if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
  if (host.empty() || !part(CURLUPART_ZONEID).empty()) { why = "bad host"; return false; }
  addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
  addrinfo* addresses = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &addresses) != 0 || !addresses) {
    why = "cannot resolve host"; return false;
  }
  std::vector<std::string> resolved;
  for (auto* p = addresses; p; p = p->ai_next) {
    char text[INET6_ADDRSTRLEN]{};
    const void* address = p->ai_family == AF_INET ?
        static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr) :
        p->ai_family == AF_INET6 ? static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(p->ai_addr)->sin6_addr) : nullptr;
    if (!address || !inet_ntop(p->ai_family, address, text, sizeof(text))) { resolved.clear(); break; }
    resolved.emplace_back(text);
  }
  freeaddrinfo(addresses);
  // Hostnames use exactly all checked addresses: libcurl must not resolve again.
  return pin_public_addresses(host,port,resolved,pinned,why);
}

ToolResult curl_fetch(const std::string& method, const std::string& url, const std::string& body) {
  std::string why, pinned;
  if (!resolve_public_url(url, pinned, why)) {
    return {false, why};
  }
  CURL* curl = curl_easy_init();
  if (!curl) {
    return {false, "curl init failed"};
  }
  curl_slist* resolve = pinned.empty() ? nullptr : curl_slist_append(nullptr, pinned.c_str());
  if (!pinned.empty() && !resolve) { curl_easy_cleanup(curl); return {false, "resolve allocation failed"}; }
  curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
  curl_easy_setopt(curl, CURLOPT_PROXY, ""); // Environment proxies must not bypass address validation.
  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  } else if (method != "GET") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }
  }
  const CURLcode rc = curl_easy_perform(curl);
  long http = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
  curl_easy_cleanup(curl);
  curl_slist_free_all(resolve);
  if (rc != CURLE_OK) {
    return {false, std::string("curl: ") + curl_easy_strerror(rc)};
  }
  std::ostringstream out;
  out << "HTTP " << http << "\n" << response;
  return {http >= 200 && http < 300, out.str()};
}

}  // namespace

ToolResult tool_http_get(const std::string& url) {
  return curl_fetch("GET", url, {});
}

ToolResult tool_http_request(const std::string& method, const std::string& url, const std::string& body) {
  std::string m = method;
  for (auto& c : m) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (m.empty()) {
    m = "GET";
  }
  return curl_fetch(m, url, body);
}

}  // namespace arli
