#include "memory.hpp"
#include "context.hpp"

namespace arli {

MemoryState MemoryState::load(const std::vector<MemoryRow>& rows, std::int64_t next_id) {
  MemoryState s;
  s.entries_ = rows;
  s.next_id_ = next_id;
  for (const auto& row : rows) s.next_id_ = std::max(s.next_id_, row.id + 1);
  return s;
}

std::string MemoryState::inject_block() const {
  if (entries_.empty()) {
    return {};
  }
  const int n = std::min(static_cast<int>(entries_.size()), kMemoryInjectLimit);
  std::string out = "\n\nПамять оператора (факты, до " + std::to_string(kMemoryInjectLimit) + "):\n";
  for (int i = 0; i < n; ++i) {
    out += "- " + entries_[static_cast<size_t>(i)].text + "\n";
  }
  return out;
}

ToolResult MemoryState::remember(const nlohmann::json& args) {
  std::string text = args.value("text", "");
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.erase(text.begin());
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  if (text.empty()) {
    return {false, "remember: нужен text"};
  }
  if (text.size() > 500) {
    text.resize(context::utf8_floor(text, 500));
  }
  for (const auto& e : entries_) {
    if (e.text == text) {
      return {true, "уже в памяти (#" + std::to_string(e.id) + ")"};
    }
  }
  if (static_cast<int>(entries_.size()) >= kMemoryStoreLimit) {
    entries_.pop_back();
  }
  MemoryRow row;
  row.id = next_id_++;
  row.text = std::move(text);
  entries_.insert(entries_.begin(), row);
  return {true, "запомнила (#" + std::to_string(row.id) + ")"};
}

ToolResult MemoryState::forget(const nlohmann::json& args) {
  if (args.contains("id") && args["id"].is_number_integer()) {
    const auto id = args["id"].get<std::int64_t>();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->id == id) {
        entries_.erase(it);
        return {true, "забыла #" + std::to_string(id)};
      }
    }
    return {false, "forget: id не найден"};
  }
  std::string text = args.value("text", "");
  if (text.empty()) {
    return {false, "forget: нужен id или text"};
  }
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->text.find(text) != std::string::npos) {
      const auto id = it->id;
      entries_.erase(it);
      return {true, "забыла #" + std::to_string(id)};
    }
  }
  return {false, "forget: совпадений нет"};
}

ToolResult MemoryState::list() const {
  if (entries_.empty()) {
    return {true, "память пуста"};
  }
  std::string out;
  const int n = std::min(static_cast<int>(entries_.size()), kMemoryStoreLimit);
  for (int i = 0; i < n; ++i) {
    const auto& e = entries_[static_cast<size_t>(i)];
    if (i) {
      out += "\n";
    }
    out += "#" + std::to_string(e.id) + " " + e.text;
  }
  return {true, out};
}

void MemoryState::fill_snapshot(v1::MemorySnapshot* out) const {
  out->clear_entries();
  for (const auto& e : entries_) {
    auto* item = out->add_entries();
    item->set_id(e.id);
    item->set_text(e.text);
  }
}

void fill_turn_event_memory(const MemoryState& mem, v1::TurnEvent& ev) {
  mem.fill_snapshot(ev.mutable_memory());
}

std::vector<MemoryRow> memory_rows_for_inject(Database& db) {
  return db.list_memory(kMemoryInjectLimit);
}

}  // namespace arli
