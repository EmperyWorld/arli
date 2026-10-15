#include "llm_models.hpp"

#include "db.hpp"
#include "llm_internal.hpp"

#include <nlohmann/json.hpp>

#include <curl/curl.h>

#include <algorithm>
#include <cctype>

namespace arli {
namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

std::string lower_ascii(std::string s) {
  for (auto& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool id_matches_model(const std::string& listed_id, const std::string& want) {
  if (listed_id.empty() || want.empty()) {
    return false;
  }
  if (listed_id == want) {
    return true;
  }
  if (listed_id.size() >= want.size() &&
      listed_id.compare(listed_id.size() - want.size(), want.size(), want) == 0) {
    return true;
  }
  if (want.find('/') != std::string::npos) {
    const auto pos = want.rfind('/');
    if (pos != std::string::npos && listed_id == want.substr(pos + 1)) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::string normalize_llm_profile(std::string profile) {
  profile = lower_ascii(std::move(profile));
  if (profile.empty() || profile == "gemma" || profile == "gemma4") {
    return "gemma4";
  }
  if (profile == "generic" || profile == "openai") {
    return "generic";
  }
  if (profile == "qwen" || profile == "qwen3" || profile == "qwen3.8" || profile == "qwen38") {
    return "qwen3";
  }
  return "gemma4";
}

LlmProbeResult probe_llm_endpoint(const LlmConfig& cfg, const std::string& model_id) {
  LlmProbeResult out;
  if (cfg.base_url.empty()) {
    out.error = "llm_base_url empty";
    return out;
  }
  const std::string url = llm_internal::join_url(cfg.base_url, "/models");

  CURL* curl = curl_easy_init();
  if (!curl) {
    out.error = "curl init failed";
    return out;
  }

  std::string response;
  struct curl_slist* headers = nullptr;
  if (!cfg.api_key.empty()) {
    const std::string auth = "Authorization: Bearer " + cfg.api_key;
    headers = curl_slist_append(headers, auth.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);

  const CURLcode rc = curl_easy_perform(curl);
  long http = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK) {
    out.error = std::string("curl: ") + curl_easy_strerror(rc);
    return out;
  }
  if (http < 200 || http >= 300) {
    out.error = "HTTP " + std::to_string(http);
    return out;
  }
  out.reachable = true;
  try {
    const auto j = nlohmann::json::parse(response);
    if (j.contains("data") && j["data"].is_array()) {
      for (const auto& item : j["data"]) {
        if (!item.is_object()) {
          continue;
        }
        const std::string id = item.value("id", "");
        if (id_matches_model(id, model_id)) {
          out.model_listed = true;
          break;
        }
      }
    }
  } catch (const std::exception& e) {
    out.error = std::string("bad json: ") + e.what();
    out.reachable = false;
  }
  if (!out.model_listed && out.error.empty()) {
    out.error = "model not in /models list";
  }
  return out;
}

LlmConfig llm_config_from_settings(const SettingsRow& s) {
  LlmConfig cfg;
  cfg.base_url = s.llm_base_url;
  cfg.model = s.llm_model;
  cfg.api_key = s.llm_api_key;
  cfg.profile = s.llm_profile;
  cfg.temperature = s.llm_temperature;
  return cfg;
}

bool llm_health_ok(const SettingsRow& s) {
  const auto probe = probe_llm_endpoint(llm_config_from_settings(s), s.llm_model);
  return probe.reachable && probe.model_listed;
}

}  // namespace arli
