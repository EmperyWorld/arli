#pragma once
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace arli {
// Rejected candidates are operations, not failures of the unchanged source file.
// Persist only typed targets, never replacement/source text, diagnostics or reasons.
class VerificationEdits {
  struct Target {
    std::string path, kind, digest;
    int first = 0, last = 0;
    bool abandoned = false;
  };
  static constexpr std::size_t limit = 32, max_path = 4096, max_state = 28000;
  std::vector<Target> targets_;
  std::set<std::string> source_errors_;
  bool overflow_ = false;
  static std::string normalized(const std::string& path) {
    return std::filesystem::path(path).lexically_normal().string();
  }
  static std::string hash(std::string text, bool normalize_newlines = true) {
    if (normalize_newlines)
      for (std::size_t p = 0; (p = text.find("\r\n", p)) != std::string::npos;) text.erase(p, 1);
    unsigned char bytes[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(text.data()), text.size(), bytes);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
  }
  static Target target(const std::string& tool, const std::string& path, const nlohmann::json& args) {
    Target result{path, "unknown", {}};
    const bool appending = tool == "append_file" || (tool == "write_file" && args.contains("append") &&
        args["append"].is_boolean() && args["append"].get<bool>());
    if (tool == "create_file" || (tool == "write_file" && !appending)) { result.kind = "create"; return result; }
    if (appending) {
      for (const char* key : {"content", "text", "body", "data"}) if (args.contains(key) && args[key].is_string()) {
        if (!args[key].get_ref<const std::string&>().empty()) {
          result.kind = "append"; result.digest = hash(args[key].get<std::string>(), false);
        }
        return result;
      }
      return result;
    }
    if (tool != "edit_file" && tool != "replace_text") return result;
    if (tool == "replace_text") {
      if (args.contains("old_text") && args["old_text"].is_string() && !args["old_text"].get_ref<const std::string&>().empty()) {
        result.kind = "text"; result.digest = hash(args["old_text"].get<std::string>());
      }
      return result;
    }
    // Match dispatch precedence: legacy edit_file uses ranges when supplied,
    // even if the model also passed an obsolete search/old_text field.
    if (tool == "edit_file" && args.contains("start_line")) {
      if (args["start_line"].is_number_integer() && args.contains("end_line") && args["end_line"].is_number_integer()) {
        const auto first = args["start_line"].get<std::int64_t>(), last = args["end_line"].get<std::int64_t>();
        if (first >= 1 && last >= first && last <= 100000000) {
          result.kind = "range"; result.first = static_cast<int>(first); result.last = static_cast<int>(last);
        }
      }
      return result;
    }
    for (const char* key : {"search", "old_text", "old", "oldText", "old_string", "oldString"}) {
      if (args.contains(key) && args[key].is_string()) {
        if (!args[key].get_ref<const std::string&>().empty()) {
          result.kind = "text"; result.digest = hash(args[key].get<std::string>());
        }
        return result;
      }
    }
    return result;
  }
  static bool same(const Target& a, const Target& b) {
    return a.path == b.path && a.kind == b.kind && a.digest == b.digest && a.first == b.first && a.last == b.last;
  }
  // Range correction must cover the rejected range; text correction must target
  // exactly the same old fragment. This does not claim the intended semantics pass.
  static bool corrects(const Target& fixed, const Target& failed) {
    if (fixed.path != failed.path || fixed.kind != failed.kind || fixed.kind == "unknown") return false;
    if (fixed.kind == "range") return fixed.first <= failed.first && fixed.last >= failed.last;
    return fixed.kind == "create" || fixed.digest == failed.digest;
  }
  static std::string label(const Target& item) {
    auto out = item.path;
    if (item.kind == "range") out += ":" + std::to_string(item.first) + "-" + std::to_string(item.last);
    return out;
  }
public:
  void result(const std::string& tool, const std::string& path, bool ok, const std::string& output,
              const nlohmann::json& args) {
    if (path.empty()) return;
    if (path.size() > max_path || path.find('\0') != std::string::npos) { overflow_ = true; return; }
    const auto file = normalized(path);
    if (tool == "check_file") {
      if (ok) source_errors_.erase(file);
      else if (output.find("SYNTAX_UNCHECKED") == std::string::npos) {
        if (source_errors_.size() < limit || source_errors_.contains(file)) source_errors_.insert(file);
        else overflow_ = true;
      }
      return;
    }
    if (tool != "edit_file" && tool != "replace_text" && tool != "append_file" &&
        tool != "create_file" && tool != "write_file") return;
    const auto operation = target(tool, file, args);
    if (ok) {
      if (output.find("SYNTAX_OK:") != std::string::npos) source_errors_.erase(file);
      std::erase_if(targets_, [&](const auto& item) { return corrects(operation, item); });
      // Another mutation can move line coordinates; retaining them could later
      // mistake a successful edit of different code for repair of this candidate.
      for (auto& item : targets_) if (item.path == file && item.kind == "range") {
        item.kind = "unknown"; item.first = item.last = 0;
      }
      return;
    }
    bool rejected = false;
    for (const char* code : {"SYNTAX_REJECTED:", "READ_REQUIRED:", "RANGE_MISMATCH:", "FILE_CHANGED:",
                            "TEXT_MISMATCH:", "TEXT_AMBIGUOUS:"})
      rejected |= output.find(code) != std::string::npos;
    if (!rejected) return;
    for (auto& item : targets_) if (same(operation, item)) { item.abandoned = false; return; }
    if (targets_.size() < limit) targets_.push_back(operation); else overflow_ = true;
  }
  bool abandon(const std::string& path, const std::string& reason) {
    if (reason.size() > 512 || reason.find_first_not_of(" \t\r\n") == std::string::npos) return false;
    bool found = false;
    for (auto& item : targets_) if (item.path == normalized(path) && !item.abandoned) {
      item.abandoned = true; found = true;
    }
    return found;
  }
  bool has_abandoned() const {
    return std::any_of(targets_.begin(), targets_.end(), [](const auto& item) { return item.abandoned; });
  }
  std::string abandoned_summary() const {
    std::string result;
    for (const auto& item : targets_) if (item.abandoned) {
      if (!result.empty()) result += "; ";
      result += "правка отложена: " + label(item);
    }
    return result;
  }
  std::string pending() const {
    if (overflow_) return "Учёт незавершённых правок неполон; нельзя подтверждать полное выполнение.";
    for (const auto& item : targets_) if (!item.abandoned)
      return "Не применена правка " + label(item) +
          ". Исправь этот же фрагмент либо явно отложи правку через abandon_edit; проверка старого файла её не закрывает.";
    return source_errors_.empty() ? std::string() : "check_file " + *source_errors_.begin();
  }
  std::string serialized() const {
    auto entries = nlohmann::json::array();
    for (const auto& item : targets_) entries.push_back({{"path", item.path}, {"kind", item.kind},
        {"digest", item.digest}, {"first", item.first}, {"last", item.last}, {"abandoned", item.abandoned}});
    auto data = nlohmann::json{{"version", 1}, {"targets", entries}, {"source_errors", source_errors_}, {"overflow", overflow_}};
    // Workflow stores this as a JSON string, so account for that second escaping
    // layer. A bounded state must never turn a successful mutation into an exception.
    while (nlohmann::json(data.dump()).dump().size() > max_state) {
      data["overflow"] = true;
      if (!data["targets"].empty()) data["targets"].erase(data["targets"].end() - 1);
      else if (!data["source_errors"].empty()) data["source_errors"].erase(data["source_errors"].end() - 1);
      else break;
    }
    return data.dump();
  }
  bool restore(const std::string& state) {
    if (state.empty()) return true;
    try {
      if (state.size() > max_state) throw std::runtime_error("state limit");
      const auto data = nlohmann::json::parse(state);
      if (data.at("version") != 1 || !data.at("targets").is_array() || !data.at("source_errors").is_array() ||
          data.at("targets").size() > limit || data.at("source_errors").size() > limit) throw std::runtime_error("state shape");
      VerificationEdits restored; restored.overflow_ = data.at("overflow").get<bool>();
      auto path = [](const nlohmann::json& value) {
        auto text = value.get<std::string>();
        if (text.empty() || text.size() > max_path || text.find('\0') != std::string::npos) throw std::runtime_error("state path");
        return normalized(text);
      };
      for (const auto& value : data.at("source_errors")) restored.source_errors_.insert(path(value));
      for (const auto& entry : data.at("targets")) {
        if (!entry.at("first").is_number_integer() || !entry.at("last").is_number_integer() ||
            entry.at("first") < 0 || entry.at("last") < 0 || entry.at("first") > 100000000 || entry.at("last") > 100000000)
          throw std::runtime_error("state coordinates");
        Target item{path(entry.at("path")), entry.at("kind").get<std::string>(), entry.at("digest").get<std::string>(),
                    entry.at("first").get<int>(), entry.at("last").get<int>(), entry.at("abandoned").get<bool>()};
        if (item.kind != "range" && item.kind != "text" && item.kind != "append" && item.kind != "create" && item.kind != "unknown")
          throw std::runtime_error("state kind");
        if (item.kind == "range" && (item.first < 1 || item.last < item.first || item.last > 100000000))
          throw std::runtime_error("state range");
        const bool fingerprint = item.kind == "text" || item.kind == "append";
        if ((fingerprint && (item.digest.size() != 64 ||
            item.digest.find_first_not_of("0123456789abcdef") != std::string::npos)) ||
            (!fingerprint && !item.digest.empty()) ||
            (item.kind != "range" && (item.first != 0 || item.last != 0))) throw std::runtime_error("state target");
        restored.targets_.push_back(std::move(item));
      }
      *this = std::move(restored); return true;
    } catch (...) { overflow_ = true; return false; }
  }
};
}
