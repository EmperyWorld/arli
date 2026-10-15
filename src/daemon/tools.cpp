#include "private_state.hpp"
#include "tools.hpp"
#include "tools/mutation_journal.hpp"

#include "mutation_diff.hpp"

#include "tools/registry.hpp"
#include "tools/write_guard.hpp"
#include "tools/atomic_publish.hpp"
#include "tools/backup.hpp"
#include "tools/file_io.hpp"
#include "tools/syntax_check.hpp"
#include "tools/replace_text.hpp"

#include "context.hpp"
#include "turn_policy.hpp"
#include "listing.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>
#include <sys/wait.h>

namespace arli {
namespace {

using json = nlohmann::json;

bool is_under(const std::filesystem::path& root, const std::filesystem::path& candidate) {
  const auto r = std::filesystem::weakly_canonical(root);
  std::error_code ec;
  const auto c = std::filesystem::weakly_canonical(candidate, ec);
  if (ec) {
    return false;
  }
  auto rit = r.begin();
  auto cit = c.begin();
  for (; rit != r.end() && cit != c.end(); ++rit, ++cit) {
    if (*rit != *cit) {
      return false;
    }
  }
  return rit == r.end();
}

// Normalize display line endings only; whitespace inside source is data.
std::string normalize_lf(std::string s) {
  for (size_t p = 0; (p = s.find("\r\n", p)) != std::string::npos;) s.erase(p, 1);
  return s;
}

// Translate an LF display offset back to original bytes without touching other text.
size_t raw_offset(const std::string& raw, size_t offset) {
  size_t i = 0, normalized = 0;
  while (i < raw.size() && normalized < offset) {
    if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
    ++i; ++normalized;
  }
  return i;
}

std::string file_newlines(std::string text, const std::string& raw) {
  const auto nl = raw.find('\n');
  if (nl == std::string::npos || nl == 0 || raw[nl - 1] != '\r') return text;
  std::string out;
  for (char c : text) { if (c == '\n') out += '\r'; out += c; }
  return out;
}

std::string rtrim_one(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
    s.pop_back();
  }
  return s;
}

std::string expand_tabs(const std::string& s, int tabw = 4) {
  std::string out;
  out.reserve(s.size());
  int col = 0;
  for (char ch : s) {
    if (ch == '\t') {
      const int n = tabw - (col % tabw);
      out.append(static_cast<size_t>(n), ' ');
      col += n;
    } else {
      out.push_back(ch);
      col = (ch == '\n') ? 0 : col + 1;
    }
  }
  return out;
}

std::string lstrip_ws(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  return s.substr(i);
}

enum class MatchMode { Exact, Rtrim, Tabs, Indent };

std::string norm_line(const std::string& line, MatchMode mode) {
  switch (mode) {
    case MatchMode::Exact:
      return line;
    case MatchMode::Rtrim:
      return rtrim_one(line);
    case MatchMode::Tabs:
      return rtrim_one(expand_tabs(line));
    case MatchMode::Indent:
      return rtrim_one(lstrip_ws(expand_tabs(line)));
  }
  return line;
}

std::vector<std::string> split_lines_keep(const std::string& s) {
  std::vector<std::string> lines;
  size_t i = 0;
  while (i <= s.size()) {
    size_t eol = s.find('\n', i);
    if (eol == std::string::npos) {
      lines.push_back(s.substr(i));
      break;
    }
    lines.push_back(s.substr(i, eol - i));
    i = eol + 1;
    if (i == s.size()) {
      lines.push_back("");
      break;
    }
  }
  return lines;
}

// Byte offset of start of line index in content (LF-normalized, no CR).
size_t line_byte_offset(const std::string& content, size_t line_idx) {
  size_t pos = 0;
  for (size_t li = 0; li < line_idx; ++li) {
    const size_t eol = content.find('\n', pos);
    if (eol == std::string::npos) {
      return content.size();
    }
    pos = eol + 1;
  }
  return pos;
}

struct Span {
  size_t pos = std::string::npos;
  size_t len = 0;
  const char* mode = "exact";
};

bool lines_match_at(const std::vector<std::string>& hay, size_t at, const std::vector<std::string>& needle,
                    MatchMode mode) {
  if (at + needle.size() > hay.size()) {
    return false;
  }
  for (size_t i = 0; i < needle.size(); ++i) {
    if (norm_line(hay[at + i], mode) != norm_line(needle[i], mode)) {
      return false;
    }
  }
  return true;
}

Span span_from_lines(const std::string& content, size_t start_line, size_t n_lines, const char* mode_tag) {
  const size_t start = line_byte_offset(content, start_line);
  if (n_lines == 0) {
    return {start, 0, mode_tag};
  }
  const size_t last_start = line_byte_offset(content, start_line + n_lines - 1);
  size_t end = content.size();
  const size_t nl = content.find('\n', last_start);
  if (nl != std::string::npos) {
    end = nl + 1;  // include terminating newline of last matched line
  }
  return {start, end - start, mode_tag};
}

Span find_unique_lines(const std::string& content, const std::vector<std::string>& hay,
                       const std::vector<std::string>& needle, MatchMode mode, const char* tag,
                       std::string& err) {
  if (needle.empty()) {
    err = "old_text пустой";
    return {};
  }
  // Drop a single trailing empty line from needle if model added extra \n
  std::vector<std::string> n = needle;
  if (n.size() > 1 && n.back().empty()) {
    n.pop_back();
  }
  int hits = 0;
  size_t hit_at = 0;
  for (size_t i = 0; i + n.size() <= hay.size(); ++i) {
    if (lines_match_at(hay, i, n, mode)) {
      ++hits;
      hit_at = i;
      if (hits > 1) {
        err = std::string("old_text неоднозначен (") + tag +
              ") — добавь окружающих строк для уникальности";
        return {};
      }
    }
  }
  if (hits == 1) {
    return span_from_lines(content, hit_at, n.size(), tag);
  }
  return {};
}

// Cheap similarity: shared prefix length after indent-norm.
int line_score(const std::string& a, const std::string& b) {
  const std::string na = norm_line(a, MatchMode::Indent);
  const std::string nb = norm_line(b, MatchMode::Indent);
  if (na.empty() || nb.empty()) {
    return 0;
  }
  size_t i = 0;
  while (i < na.size() && i < nb.size() && na[i] == nb[i]) {
    ++i;
  }
  // Penalize length mismatch lightly.
  const int base = static_cast<int>(i);
  const int diff = static_cast<int>(std::abs(static_cast<long>(na.size()) - static_cast<long>(nb.size())));
  return base * 2 - diff;
}

std::string near_match_hint(const std::string& content, const std::string& needle) {
  const auto hay = split_lines_keep(content);
  const auto ned = split_lines_keep(needle);
  std::string probe;
  for (const auto& ln : ned) {
    if (!rtrim_one(lstrip_ws(ln)).empty()) {
      probe = ln;
      break;
    }
  }
  if (probe.empty() || hay.empty()) {
    return {};
  }
  int best = -1;
  size_t best_i = 0;
  for (size_t i = 0; i < hay.size(); ++i) {
    const int sc = line_score(hay[i], probe);
    if (sc > best) {
      best = sc;
      best_i = i;
    }
  }
  if (best < 4) {
    return {};
  }
  auto clip = [](std::string s) {
    s = rtrim_one(s);
    if (s.size() > 80) {
      s = context::clip_text(std::move(s), 79);
    }
    return s;
  };
  return "near L" + std::to_string(best_i + 1) + ": `" + clip(hay[best_i]) + "` | твой old: `" +
         clip(probe) + "`";
}

// Gemma thrash: old_text="/**" or "{". Require a real unique snippet from read_file.
bool old_text_too_weak(const std::string& needle, std::string& why) {
  const auto lines = split_lines_keep(needle);
  int nonempty = 0;
  size_t significant = 0;
  std::string first;
  for (const auto& ln : lines) {
    const std::string t = rtrim_one(lstrip_ws(ln));
    if (t.empty()) {
      continue;
    }
    ++nonempty;
    significant += t.size();
    if (first.empty()) {
      first = t;
    }
  }
  if (nonempty == 0 || significant == 0) {
    why = "old_text пустой — скопируй точный фрагмент из read_file";
    return true;
  }
  static const char* kStubs[] = {"/**", "*/", "/*", "//", "{", "}", "(", ")", "[", "]", ";", ",", "...", "*", "**"};
  for (const char* s : kStubs) {
    if (first == s && nonempty < 3) {
      why = "old_text слишком короткий/общий (`";
      why += s;
      why += "`) — возьми ≥2–3 строк кода из read_file, не только открывающий комментарий";
      return true;
    }
  }
  // One short line OK if unique content (e.g. </html>); stubs already caught.
  if (nonempty < 2 && significant < 7) {
    why = "old_text слишком короткий — нужно ≥2 непустых строк или ≥7 символов уникального фрагмента";
    return true;
  }
  return false;
}

// Cascade: exact → rtrim → tabs↔spaces → indent-flex (Claude/Tesslate-style).
Span find_unique(const std::string& haystack, const std::string& needle, std::string& err) {
  if (needle.empty()) {
    err = "old_text пустой — укажи точный фрагмент с диска";
    return {};
  }
  if (old_text_too_weak(needle, err)) {
    return {};
  }

  // Fast path: exact substring (unique).
  {
    size_t pos = haystack.find(needle);
    if (pos != std::string::npos) {
      if (haystack.find(needle, pos + needle.size()) != std::string::npos) {
        err = "old_text совпадает больше одного раза — добавь окружающих строк для уникальности";
        return {};
      }
      return {pos, needle.size(), "exact"};
    }
  }

  const auto hay = split_lines_keep(haystack);
  const auto ned = split_lines_keep(needle);

  Span s = find_unique_lines(haystack, hay, ned, MatchMode::Rtrim, "fuzzy_ws", err);
  if (s.pos != std::string::npos) {
    return s;
  }
  err.clear();
  s = find_unique_lines(haystack, hay, ned, MatchMode::Tabs, "fuzzy_tab", err);
  if (s.pos != std::string::npos) {
    return s;
  }
  err.clear();
  s = find_unique_lines(haystack, hay, ned, MatchMode::Indent, "fuzzy_indent", err);
  if (s.pos != std::string::npos) {
    return s;
  }

  std::string hint = near_match_hint(haystack, needle);
  err = "old_text not found — read_file и скопируй точный фрагмент (проверь tabs/spaces)";
  if (!hint.empty()) {
    err += " | " + hint;
  }
  return {};
}

std::string read_regular_file(const std::filesystem::path& file, std::string& err) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) {
    err = "file not found: " + file.string();
    return {};
  }
  if (std::filesystem::is_directory(file, ec)) {
    err = "path is a directory, not a file: " + file.string();
    return {};
  }
  if (!std::filesystem::is_regular_file(file, ec)) {
    err = "not a regular file: " + file.string();
    return {};
  }
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    err = "cannot open: " + file.string();
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Atomic write: temp in same dir → rename.
ToolResult atomic_write(const std::filesystem::path& workdir, const std::filesystem::path& file,
                        const std::string& content, bool do_backup, const std::string* expected = nullptr) {
  try {
    const auto syntax = check_source_syntax(workdir, file.string(), content);
    if (syntax.checked && !syntax.ok)
      return {false, "SYNTAX_REJECTED: правка не записана, файл и резервная копия не изменены. "
          "Ошибка в собранном результате (номера относятся к новой версии):\n" + syntax.detail +
          "\nПроверь границы замены и соседний код через read_file. Исправь text или диапазон. "
          "Если исходный файл уже повреждён, включи повреждённый участок в исправление."};
    // Parsing can take time; do not overwrite a concurrent editor's newer version.
    if (expected) {
      std::string error;
      if (read_regular_file(file, error) != *expected || !error.empty())
        return {false, "READ_REQUIRED: файл изменился во время проверки. Перечитай диапазон; правка не записана."};
    }
    tools_internal::FileFd parent(tools_internal::open_parent(file, true));
    if (do_backup) {
      const auto backup = tools_internal::backup_regular_file(workdir, file, expected);
      if (!backup.ok) return backup;
    }
    const auto published = publish_file(file, content, do_backup, expected);
    if (!published.ok) return published;
    return {true, syntax.checked ? "SYNTAX_OK: проверен синтаксис всего файла; поведение ещё требует тестов." : syntax.detail};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

}  // namespace

std::filesystem::path resolve_under(const std::filesystem::path& workdir, const std::string& rel) {
  std::filesystem::create_directories(workdir);
  const auto root = std::filesystem::weakly_canonical(workdir);
  std::filesystem::path raw(rel.empty() ? "." : rel);
  if (raw.is_absolute()) {
    throw std::runtime_error("path must be relative to workdir");
  }
  const auto joined = root / raw;
  std::error_code ec;
  const auto canon = std::filesystem::weakly_canonical(joined, ec);
  const auto probe = ec ? joined.lexically_normal() : canon;
  if (!is_under(root, probe) && probe != root)
    throw std::runtime_error("path escapes workdir");
  guard_private_state(probe);
  return probe;
}

ToolResult tool_list_dir(const std::filesystem::path& workdir, const std::string& path,
                          const std::string& list_ignore) {
  try {
    const auto dir = resolve_under(workdir, path.empty() ? "." : path);
    if (!std::filesystem::exists(dir)) {
      return {false, "path not found: " + dir.string()};
    }
    if (!std::filesystem::is_directory(dir)) {
      return {false, "not a directory: " + dir.string()};
    }
    const auto skip = listing::names(list_ignore);
    std::ostringstream out;
    std::string hidden;
    for (const auto& ent : std::filesystem::directory_iterator(dir)) {
      if (private_state_path(ent.path())) continue;
      const std::string name = ent.path().filename().string();
      if (listing::is_name(name, skip)) {
        if (!hidden.empty()) {
          hidden += ", ";
        }
        hidden += name;
        continue;
      }
      out << (ent.is_directory() ? "d " : "f ") << name << '\n';
    }
    if (!hidden.empty()) {
      out << "скрыто: " << hidden << '\n';
    }
    return {true, out.str()};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

namespace {

std::vector<std::string> split_file_lines(const std::string& data, bool& trailing_nl) {
  std::string norm = normalize_lf(data);
  trailing_nl = !norm.empty() && norm.back() == '\n';
  if (!norm.empty() && norm.back() == '\n') {
    norm.pop_back();
  }
  std::vector<std::string> lines;
  if (norm.empty() && data.empty()) {
    return lines;
  }
  std::string cur;
  for (char c : norm) {
    if (c == '\n') {
      lines.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  lines.push_back(cur);
  return lines;
}

std::string join_file_lines(const std::vector<std::string>& lines, bool trailing_nl) {
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i) {
      out.push_back('\n');
    }
    out += lines[i];
  }
  if (trailing_nl && !lines.empty()) {
    out.push_back('\n');
  }
  return out;
}

}  // namespace

ToolResult tool_read_file(const std::filesystem::path& workdir, const std::string& path, int max_bytes,
                          int start_line, int end_line) {
  try {
    if (path.empty() || path == "." || path == "./") {
      return {false, "path is a directory, not a file — укажи имя файла"};
    }
    const auto file = resolve_under(workdir, path);
    std::string err;
    std::string data = read_regular_file(file, err);
    if (!err.empty() && data.empty()) {
      return {false, err};
    }
    if (max_bytes <= 0) return {false, "max_bytes должен быть положительным"};
    if (data.empty()) return {true, "# " + path + " lines=0 (empty file). Используй append_file для записи.", {}, {0,0,0,tools_internal::mutation_hash(data)}};
    bool trailing = false;
    const auto lines = split_file_lines(data, trailing);
    const int total = static_cast<int>(lines.size());
    int from = start_line < 1 ? 1 : start_line;
    if (from > total) return {false, "start_line за концом файла (lines=" + std::to_string(total) + ")"};
    int to = end_line < from ? from + std::min(199, total - from) : end_line;
    if (to - from > 399) {
      to = from + 399;
    }
    if (from > total) {
      return {false, "start_line за концом файла (lines=" + std::to_string(total) + ")"};
    }
    if (to > total) {
      to = total;
    }
    size_t bytes = 0;
    int shown_to = from - 1;
    for (int n = from; n <= to; ++n) {
      const size_t size = lines[n - 1].size() + std::to_string(n).size() + 2;
      if (size > static_cast<size_t>(max_bytes) - bytes) break;
      bytes += size; shown_to = n;
    }
    if (shown_to < from) return {false, "LINE_TOO_LONG: строка " + std::to_string(from) +
        " превышает бюджет read_file; частичная строка не даёт права на правку."};
    to = shown_to;
    std::ostringstream out;
    out << "# " << path << " lines " << from << "-" << to << " of " << total
        << " bytes=" << data.size() << " final_newline=" << (trailing ? "yes" : "no")
        << "  (формат N|текст — для replace_text копируй точный уникальный old_text без N|; сначала прочитай весь изменяемый фрагмент)\n";
    if (end_line > to && to < total)
      out << "# requested end_line=" << end_line << "; показаны только строки " << from << "-" << to
          << ". Остальные строки НЕ прочитаны. Продолжи read_file start_line=" << to + 1
          << " end_line=" << std::min(end_line, total) << " перед правкой всего диапазона.\n";
    for (int n = from; n <= to; ++n) {
      out << n << '|' << lines[static_cast<size_t>(n - 1)] << '\n';
    }
    if (to < total) {
      out << "# next: read_file path=" << path << " start_line=" << (to + 1) << "\n";
    }
    return {true, out.str(), {}, {from,to,total,tools_internal::mutation_hash(data)}};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

std::string change_preview(const std::vector<std::string>& removed, const std::vector<std::string>& added) {
  std::string out = " | +" + std::to_string(added.size()) + " -" + std::to_string(removed.size()) + "\n";
  const int cap = 8;
  for (int i = 0; i < static_cast<int>(removed.size()) && i < cap; ++i) {
    out += "- ";
    out += context::clip_text(removed[i], 140);
    out += "\n";
  }
  if (static_cast<int>(removed.size()) > cap) {
    out += "- ...\n";
  }
  for (int i = 0; i < static_cast<int>(added.size()) && i < cap; ++i) {
    out += "+ ";
    out += context::clip_text(added[i], 140);
    out += "\n";
  }
  if (static_cast<int>(added.size()) > cap) {
    out += "+ ...\n";
  }
  return out;
}

ToolResult tool_edit_lines(const std::filesystem::path& workdir, const std::string& path, int start_line,
                           int end_line, const std::string& new_text, const std::string* expected) {
  try {
    if (path.empty() || path == "." || path == "./") {
      return {false, "path is a directory, not a file — укажи имя файла"};
    }
    if (start_line < 1) {
      return {false, "start_line должен быть ≥ 1 (номер из read_file, префикс N|)"};
    }
    if (end_line < start_line) {
      end_line = start_line;
    }
    const auto file = resolve_under(workdir, path);
    std::string err;
    const std::string raw = read_regular_file(file, err);
    if (!err.empty()) {
      return {false, err};
    }
    if (expected && raw != *expected) return {false, "READ_REQUIRED: файл изменился после чтения; перечитай диапазон."};
    bool trailing = false;
    auto lines = split_file_lines(normalize_lf(raw), trailing);
    const int total = static_cast<int>(lines.size());
    if (start_line > total) {
      return {false, "start_line " + std::to_string(start_line) + " > lines " + std::to_string(total)};
    }
    if (end_line > total) {
      return {false, "end_line " + std::to_string(end_line) + " > lines " + std::to_string(total) +
                         " — перечитай файл и укажи существующий диапазон"};
    }
    if (const auto guard = write_guard_error(path, new_text); !guard.empty()) return {false, guard};
    bool repl_trailing = false;
    auto repl = split_file_lines(normalize_lf(new_text), repl_trailing);
    if (new_text.empty()) {
      repl.clear();
    }
    const int span = end_line - start_line + 1;
    // Near-empty stub wiping a wide range — still refuse (silent code deletion).
    if (span >= 15 && static_cast<int>(repl.size()) <= 2) {
      return {false,
              "new_text почти пустой (" + std::to_string(repl.size()) + " строк) на диапазоне " +
                  std::to_string(span) +
                  ". Сузь start_line/end_line или дай полный new_text. "
                  "Чтобы дописать в конец — append_file."};
    }
    std::vector<std::string> next;
    next.insert(next.end(), lines.begin(), lines.begin() + (start_line - 1));
    next.insert(next.end(), repl.begin(), repl.end());
    next.insert(next.end(), lines.begin() + end_line, lines.end());
    const auto begin = line_byte_offset(raw, start_line - 1);
    const auto end = line_byte_offset(raw, end_line);
    const auto replacement = file_newlines(join_file_lines(repl, end < raw.size() || trailing), raw);
    const std::string body = raw.substr(0, begin) + replacement + raw.substr(end);
    if (body == raw) return {false, "NO_CHANGE: выбранный диапазон уже содержит new_text. Файл не изменён; ищи фактическую ошибку или заверши проверку."};
    const std::size_t before = raw.size();
    auto written = atomic_write(workdir, file, body, true, &raw);
    if (written.ok) {
      std::vector<std::string> removed(lines.begin() + (start_line - 1), lines.begin() + end_line);
      attach_mutation_diff(written, path, removed, repl, start_line);
      written.output = "edited " + file.string() + " lines " + std::to_string(start_line) + "-" +
                       std::to_string(end_line) + " | " + std::to_string(before) + " → " +
                       std::to_string(body.size()) +
                       "; new_range=" + std::to_string(start_line) + "-" + std::to_string(start_line + repl.size() - 1) +
                       "; lines=" + std::to_string(next.size()) + "; following_shift=" +
                       std::to_string(static_cast<int>(repl.size()) - span) + "\n" + written.output +
                       "\nПрежние номера строк устарели. Перечитай изменённый участок с соседними строками." + change_preview(removed, repl);
    }
    return written;
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_create_file(const std::filesystem::path& workdir, const std::string& path, const std::string& content) {
  try {
    if (path.empty() || path == "." || path == "./") {
      return {false, "path is a directory, not a file"};
    }
    const auto file = resolve_under(workdir, path);
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
      if (std::filesystem::is_directory(file, ec)) {
        return {false, "path is a directory: " + path};
      }
      return {false, "файл " + path +
                         " уже есть — НЕ вызывай create_file. "
                         "edit_file с start_line и end_line из read_file (префикс N|)."};
    }
    if (content.empty()) {
      return {false, "не создаю пустой файл"};
    }
    if (const auto guard = write_guard_error(path, content); !guard.empty()) return {false, guard};
    auto r = atomic_write(workdir, file, content, false);
    if (r.ok) {
      bool trail = false;
      const auto added = split_file_lines(normalize_lf(content), trail);
      attach_mutation_diff(r, path, {}, added);
      r.output = "created " + std::to_string(content.size()) + " bytes → " + file.string() +
                 "\n" + r.output + change_preview({}, added);
    }
    return r;
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_append_file(const std::filesystem::path& workdir, const std::string& path, const std::string& content,
                            const std::string* expected) {
  try {
    if (path.empty() || path == "." || path == "./") {
      return {false, "path is a directory, not a file"};
    }
    const auto file = resolve_under(workdir, path);
    std::string err;
    const std::string existing = read_regular_file(file, err);
    if (!err.empty()) {
      return {false, err + " — append_file только к существующему файлу"};
    }
    if (content.empty()) {
      return {false, "пустой append"};
    }
    if (expected && existing != *expected) return {false, "READ_REQUIRED: файл изменился после чтения; перечитай конец файла."};
    if (const auto guard = write_guard_error(path, content); !guard.empty()) return {false, guard};
    const std::string next = existing + content;
    auto r = atomic_write(workdir, file, next, true, &existing);
    if (r.ok) {
      bool trail = false;
      const auto added = split_file_lines(normalize_lf(content), trail);
      attach_mutation_diff(r, path, {}, added);
      r.output = "appended " + std::to_string(content.size()) + " bytes, total " + std::to_string(next.size()) +
                 " → " + file.string() + "\n" + r.output + change_preview({}, added);
    }
    return r;
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

static ToolResult edit_text(const std::filesystem::path& workdir, const std::string& path, const std::string& old_text,
                           const std::string& new_text, const std::string* expected, bool strict) {
  try {
    if (path.empty() || path == "." || path == "./") {
      return {false, "path is a directory, not a file — укажи имя файла (index.html и т.п.)"};
    }
    const auto file = resolve_under(workdir, path);
    std::string err;
    std::string raw = read_regular_file(file, err);
    if (!err.empty()) {
      return {false, err};
    }
    if (expected && raw != *expected) return {false, "READ_REQUIRED: файл изменился после чтения; перечитай диапазон."};
    const std::string content = normalize_lf(raw);
    const std::string needle = normalize_lf(old_text);
    const std::string replacement = normalize_lf(new_text);

    if (const auto guard = write_guard_error(path, replacement); !guard.empty()) return {false, guard};
    if (needle == replacement) return {false, "NO_CHANGE: old_text и new_text одинаковы. Файл не изменён; ищи фактическую ошибку или заверши проверку."};
    Span span;
    if (strict) {
      if (needle.empty()) return {false, "replace_text: old_text пуст; файл не изменён"};
      const auto pos = content.find(needle);
      if (pos == std::string::npos) return {false, "TEXT_MISMATCH: old_text не найден точно. Перечитай фрагмент; файл не изменён."};
      if (content.find(needle, pos + 1) != std::string::npos)
        return {false, "TEXT_AMBIGUOUS: old_text встречается несколько раз. Добавь соседний код; файл не изменён."};
      span = {pos, needle.size(), "exact"};
    } else span = find_unique(content, needle, err);
    if (span.pos == std::string::npos) {
      return {false, err};
    }

    const auto begin = raw_offset(raw, span.pos);
    const auto end = raw_offset(raw, span.pos + span.len);
    std::string inserted = replacement;
    // Fuzzy whole-line matches include the separator; do not join the following line.
    if (std::strcmp(span.mode, "exact") != 0 && span.len && content[span.pos + span.len - 1] == '\n' &&
        !inserted.empty() && inserted.back() != '\n') inserted += '\n';
    std::string next = raw.substr(0, begin) + file_newlines(inserted, raw) + raw.substr(end);

    const std::size_t before = raw.size();
    auto written = atomic_write(workdir, file, next, true, &raw);
    if (written.ok) {
      bool old_trail = false;
      bool new_trail = false;
      const auto removed = split_file_lines(needle, old_trail);
      const auto added = split_file_lines(replacement, new_trail);
      attach_mutation_diff(written, path, removed, added);
      written.output =
          "edited " + file.string() + " | " + std::to_string(before) + " → " + std::to_string(next.size()) +
          " | Δ " + std::to_string(static_cast<long long>(next.size()) - static_cast<long long>(before)) +
          "\n" + written.output + change_preview(removed, added);
      if (span.mode && std::strcmp(span.mode, "exact") != 0) {
        written.output += " | ";
        written.output += span.mode;
      }
    }
    return written;
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_edit_file(const std::filesystem::path& workdir, const std::string& path, const std::string& old_text,
                          const std::string& new_text, const std::string* expected) {
  return edit_text(workdir, path, old_text, new_text, expected, false);
}

ToolResult tool_replace_text(const std::filesystem::path& workdir, const std::string& path,
                             const std::string& old_text, const std::string& new_text, const std::string& expected) {
  return edit_text(workdir, path, old_text, new_text, &expected, true);
}

ToolResult tool_rg_search(const std::filesystem::path& workdir, const std::string& pattern, const std::string& path,
                           const std::string& list_ignore) {
  if (pattern.empty()) {
    return {false, "empty pattern"};
  }
  try {
    const auto root = resolve_under(workdir, path.empty() ? "." : path);
    std::vector<std::string> args = {"/usr/bin/rg", "--no-config", "--no-follow", "-n", "--no-heading", "-S", "--max-count", "80"};
    if (!listing::command_mentions(path, listing::names(list_ignore)))
      for (const auto& name : listing::names(list_ignore)) {
        args.insert(args.end(), {"--glob", "!**/" + name + "/**", "--glob", "!" + name});
      }
    args.insert(args.end(), {"--", pattern, root.string()});
    auto result = tools_internal::run_read_only(workdir, args, 15);
    if (!result.ok && result.output.rfind("exit 1\n", 0) == 0) return {true, "(нет совпадений)"};
    return result;
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

namespace {

std::string attempt_json_repair(std::string s) {
  std::string cleaned;
  std::vector<char> closers;
  bool in_string = false;
  bool escaped = false;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (in_string) {
      cleaned.push_back(c);
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') in_string = true;
    else if (c == '{') closers.push_back('}');
    else if (c == '[') closers.push_back(']');
    else if (c == '}' || c == ']') {
      if (closers.empty() || closers.back() != c) return s;
      closers.pop_back();
    } else if (c == ',') {
      size_t next = i + 1;
      while (next < s.size() && std::isspace(static_cast<unsigned char>(s[next]))) ++next;
      if (next < s.size() && (s[next] == '}' || s[next] == ']')) continue;
    }
    cleaned.push_back(c);
  }
  // Never reconstruct an incomplete file-content string.
  if (in_string) return s;
  while (!closers.empty()) {
    cleaned.push_back(closers.back());
    closers.pop_back();
  }
  return cleaned;
}

std::string json_keys_list(const json& args) {
  if (!args.is_object()) {
    return "(not object)";
  }
  std::string out;
  for (auto it = args.begin(); it != args.end(); ++it) {
    if (!out.empty()) out += ',';
    out += it.key();
  }
  return out.empty() ? "(none)" : out;
}

std::string require_path(const json& args, const char* tool) {
  const std::string path = coerce_path_arg(args);
  if (path.empty() || path == "." || path == "./") {
    return std::string(tool) + ": path отсутствует или пуст (keys=" + json_keys_list(args) +
           "). Нужен относительный файл, напр. index.html. "
           "Gemma часто забывает path — повтори вызов с path.";
  }
  return {};
}

std::string coerce_str(const json& args, std::initializer_list<const char*> keys) {
  for (const char* k : keys) {
    if (!args.contains(k)) continue;
    const auto& v = args.at(k);
    if (v.is_string()) {
      const std::string s = v.get<std::string>();
      if (!s.empty()) return s;
    }
  }
  return {};
}

}  // namespace

bool parse_tool_args(const std::string& args_json, nlohmann::json& out, std::string& out_err) {
  out = json::object();
  if (args_json.empty()) {
    return true;
  }
  try {
    out = json::parse(args_json);
    if (!out.is_object()) {
      out_err = "args must be a JSON object";
      return false;
    }
    return true;
  } catch (...) {
  }
  try {
    out = json::parse(attempt_json_repair(args_json));
    if (!out.is_object()) {
      out_err = "args must be a JSON object (after repair)";
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    out_err = std::string("bad args_json: ") + e.what();
    return false;
  }
}

std::string coerce_path_arg(const nlohmann::json& args) {
  return coerce_str(args, {"path", "file", "file_path", "filePath", "filepath", "filename", "target",
                           "name"});
}

std::string infer_path_from_reads(const nlohmann::json& args,
                                  const std::vector<std::pair<std::string, std::string>>& recent_reads) {
  if (!coerce_path_arg(args).empty() || recent_reads.empty()) {
    return {};
  }
  auto lf = [](std::string s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '\r') {
        if (i + 1 < s.size() && s[i + 1] == '\n') continue;
        o.push_back('\n');
      } else {
        o.push_back(s[i]);
      }
    }
    return o;
  };
  auto unique_paths = [&]() {
    std::vector<std::string> u;
    for (const auto& pr : recent_reads) {
      bool seen = false;
      for (const auto& x : u) {
        if (x == pr.first) {
          seen = true;
          break;
        }
      }
      if (!seen) u.push_back(pr.first);
    }
    return u;
  };

  const std::string old_t =
      coerce_str(args, {"old_text", "old", "oldText", "old_string", "oldString", "search"});
  if (!old_t.empty()) {
    auto unnumber = [&](const std::string& raw) {
      std::istringstream in(raw);
      std::string line, out;
      const bool numbered = raw.rfind("# ", 0) == 0;
      while (std::getline(in, line)) {
        if (numbered) {
          const auto bar = line.find('|');
          if (bar == std::string::npos || bar == 0 ||
              !std::all_of(line.begin(), line.begin() + bar, [](unsigned char c) { return std::isdigit(c); })) continue;
          line.erase(0, bar + 1);
        }
        out += line + "\n";
      }
      return out;
    };
    const std::string needle = lf(old_t);
    std::vector<std::string> hits;
    for (const auto& pr : recent_reads) {
      if (lf(unnumber(pr.second)).find(needle) == std::string::npos) continue;
      bool seen = false;
      for (const auto& h : hits) {
        if (h == pr.first) {
          seen = true;
          break;
        }
      }
      if (!seen) hits.push_back(pr.first);
    }
    if (hits.size() == 1) {
      return hits.front();
    }
    return {};  // ambiguous across files
  }
  // append without path: only if a single file was read.
  const auto u = unique_paths();
  if (u.size() == 1) {
    return u.front();
  }
  return {};
}

std::string args_with_path(const std::string& args_json, const std::string& path) {
  json args;
  std::string err;
  if (!parse_tool_args(args_json, args, err)) {
    args = json::object();
  }
  args["path"] = path;
  return args.dump();
}

ToolResult dispatch_tool(const std::filesystem::path& workdir, const std::string& name, const std::string& args_json,
                         const std::string& list_ignore, bool read_only) {
  if (read_only && tool_writes_workdir(name)) {
    return {false, "read_only: инструмент изменяет файлы — отключён"};
  }
  json args;
  std::string perr;
  if (!parse_tool_args(args_json, args, perr)) {
    return {false, perr};
  }
  if (name == "list_dir") {
    const std::string p = coerce_path_arg(args);
    return tool_list_dir(workdir, p.empty() ? "." : p, list_ignore);
  }
  if (name == "read_file") {
    if (auto e = require_path(args, "read_file"); !e.empty()) return {false, e};
    int start = 0;
    int end = 0;
    if (args.contains("start_line") && args["start_line"].is_number()) {
      start = args["start_line"].get<int>();
    }
    if (args.contains("end_line") && args["end_line"].is_number()) {
      end = args["end_line"].get<int>();
    }
    return tool_read_file(workdir, coerce_path_arg(args), 120000, start, end);
  }
  if (name == "create_file") {
    if (auto e = require_path(args, "create_file"); !e.empty()) return {false, e};
    const std::string content = coerce_str(args, {"content", "text", "body", "data"});
    if (content.empty()) {
      return {false, "create_file: content пуст (keys=" + json_keys_list(args) + ")"};
    }
    return tool_create_file(workdir, coerce_path_arg(args), content);
  }
  if (name == "append_file") {
    if (auto e = require_path(args, "append_file"); !e.empty()) return {false, e};
    return tool_append_file(workdir, coerce_path_arg(args),
                            coerce_str(args, {"content", "text", "body", "data"}));
  }
  if (name == "replace_text") {
    if (const auto error = replace_text_args_error(args); !error.empty()) return {false, error};
    return {false, "READ_REQUIRED: replace_text требует свежий read_file и проверку снимка через ReadEditGuard; файл не изменён."};
  }
  if (name == "batch_replace") {
    return {false, "READ_REQUIRED: batch_replace требует свежий read_file для каждой замены; файлы не изменены."};
  }
  if (name == "edit_file") {
    bool has_replacement = false;
    for (const char* key : {"text", "new_text", "new", "newText", "new_string", "newString", "replace"})
      if (args.contains(key) && args[key].is_string()) has_replacement = true;
    if (!has_replacement) return {false, "edit_file: нужен text (строка); отсутствующий текст не означает удаление"};
    if (auto e = require_path(args, "edit_file"); !e.empty()) return {false, e};
    const std::string path = coerce_path_arg(args);
    int start = 0;
    int end = 0;
    if (args.contains("start_line") && args["start_line"].is_number()) {
      start = args["start_line"].get<int>();
    }
    if (args.contains("end_line") && args["end_line"].is_number()) {
      end = args["end_line"].get<int>();
    }
    const std::string new_t =
        coerce_str(args, {"text", "new_text", "new", "newText", "new_string", "newString", "replace"});
    if (start >= 1) {
      return tool_edit_lines(workdir, path, start, end > 0 ? end : start, new_t);
    }
    const std::string old_t =
        coerce_str(args, {"old_text", "old", "oldText", "old_string", "oldString", "search"});
    if (old_t.empty()) {
      return {false,
              "edit_file: отсутствует search (keys=" + json_keys_list(args) + "). "
              "Передай path, search (уникальный исходный фрагмент), затем text. "
              "Читай нужный участок и исправляй его; не повторяй большой текст без исправления аргументов."};
    }
    return tool_edit_file(workdir, path, old_t, new_t);
  }
  // Legacy aliases: never stamp existing files.
  if (name == "write_file") {
    if (auto e = require_path(args, "write_file"); !e.empty()) return {false, e};
    const std::string path = coerce_path_arg(args);
    const std::string content = coerce_str(args, {"content", "text", "body", "data"});
    if (args.value("append", false)) {
      return tool_append_file(workdir, path, content);
    }
    return tool_create_file(workdir, path, content);
  }
  if (name == "rg_search") {
    const std::string p = coerce_path_arg(args);
    return tool_rg_search(workdir, coerce_str(args, {"pattern", "query", "q"}), p.empty() ? "." : p, list_ignore);
  }
  if (name == "run_shell" || name == "start_background") {
    return {false, "run_shell requires ConfirmHub gate in turn loop"};
  }
  if (name == "ask_user") {
    return {false, "ask_user is handled in the turn loop"};
  }
  if (auto ext = tools_registry::dispatch(workdir, name, args, list_ignore, read_only)) {
    return *ext;
  }
  return {false, "unknown tool: " + name};
}

std::string tools_openai_schema_json() {
  json tools = json::array();
  auto add = [&](const char* name, const char* desc, json params) {
    tools.push_back({
        {"type", "function"},
        {"function", {{"name", name}, {"description", desc}, {"parameters", std::move(params)}}},
    });
  };
  add("list_dir", "List files in a relative directory under workdir.",
      {{"type", "object"},
       {"properties", {{"path", {{"type", "string"}}}}},
       {"required", json::array()}});
  add("read_file",
      "Read a text file. Returns numbered lines N|text. Optional start_line/end_line window (default 200 lines).",
      {{"type", "object"},
       {"properties",
        {{"path", {{"type", "string"}}},
         {"start_line", {{"type", "integer"}}},
         {"end_line", {{"type", "integer"}}}}},
       {"required", {"path"}}});
  add("create_file",
      "Create a NEW file only when the path does not exist. REQUIRED relative FILE path. "
      "If file exists: do NOT retry create_file — read_file then edit_file. "
      "Do not invent parallel modules (utils.js/windowManager.js) when core.js/apps.js already cover the role.",
      {{"type", "object"},
       {"properties",
        {{"path", {{"type", "string"}, {"description", "REQUIRED relative file path, e.g. index.html"}}},
         {"content", {{"type", "string"}}}}},
       {"required", {"path", "content"}}});
  add("append_file",
      "Append content to the end of an existing file. For changes inside a file use edit_file. REQUIRED path.",
      {{"type", "object"},
       {"properties",
        {{"path", {{"type", "string"}, {"description", "REQUIRED relative file path"}}},
         {"content", {{"type", "string"}, {"description", "Text to append"}}}}},
       {"required", {"path", "content"}}});
  add("replace_text",
      "Replace one unique exact source fragment in an existing file. Prefer this for localized edits. "
      "Read every affected line with read_file in this turn, then copy old_text without N| prefixes. "
      "Supply explicit path, nonempty old_text and new_text (empty string deletes). No fuzzy matching. "
      "Missing, ambiguous, unread or stale source is rejected. Read again after a successful write.",
      {{"type", "object"}, {"additionalProperties", false},
       {"properties", {{"path", {{"type", "string"}}}, {"old_text", {{"type", "string"}, {"minLength", 1}}},
                       {"new_text", {{"type", "string"}}}}},
       {"required", {"path", "old_text", "new_text"}}});
  add("batch_replace",
      "Apply 2-16 exact replacements as one checked batch. Read every affected line of every file first. "
      "All paths must be distinct. The whole batch is validated before writing; a publication failure triggers rollback.",
      {{"type", "object"}, {"additionalProperties", false},
       {"properties", {{"edits", {{"type", "array"}, {"minItems", 2}, {"maxItems", 16},
          {"items", {{"type", "object"}, {"additionalProperties", false},
            {"properties", {{"path", {{"type", "string"}}},
                            {"old_text", {{"type", "string"}, {"minLength", 1}}},
                            {"new_text", {{"type", "string"}}}}},
            {"required", {"path", "old_text", "new_text"}}}}}}}},
       {"required", {"edits"}}});
  add("edit_file",
      "Replace an inclusive line range with text. REQUIRED path, start_line, end_line, text. "
      "First read this entire range with read_file in this turn. Numbers are 1-based from N|text. "
      "Edits without fresh reads are rejected. After every write read again before the next edit. "
      "Supply first_line and last_line copied from the selected old range to verify its boundaries. "
      "Do not include N| prefixes in text. Preserve surrounding code; no need to repeat old source.",
      {{"type", "object"},
       {"properties",
        {{"path", {{"type", "string"}}},
         {"start_line", {{"type", "integer"}, {"minimum", 1}}},
         {"end_line", {{"type", "integer"}, {"minimum", 1}}},
         {"first_line", {{"type", "string"}, {"description", "Exact OLD first line of the selected range, without N|."}}},
         {"last_line", {{"type", "string"}, {"description", "Exact OLD last line of the selected range, without N|."}}},
         {"text", {{"type", "string"}, {"description", "Replacement for the entire inclusive range; no N| gutters."}}}}},
       {"required", {"path", "start_line", "end_line", "text"}}});
  add("rg_search", "Search text with ripgrep under workdir.",
      {{"type", "object"},
       {"properties", {{"pattern", {{"type", "string"}}}, {"path", {{"type", "string"}}}}},
       {"required", {"pattern"}}});
  add("file_pass_set",
      "Set ordered file queue for this dialog (orchestrator persists). Does not read files.",
      {{"type", "object"},
       {"properties", {{"paths", {{"type", "array"}, {"items", {{"type", "string"}}}}}}},
       {"required", {"paths"}}});
  add("file_pass_review", "Mark the current queue file reviewed without changes. Read it first; give a reason.",
      {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}, {"note", {{"type", "string"}}}}},
       {"required", {"path", "note"}}});
  add("file_pass_clear", "Clear the file pass queue for this dialog.", {{"type", "object"}, {"properties", json::object()}});
  add("plan_set", "Set ordered plan steps for this dialog (orchestrator persists).",
      {{"type", "object"},
       {"properties", {{"steps", {{"type", "array"}, {"items", {{"type", "string"}}}}}}},
       {"required", {"steps"}}});
  add("plan_advance", "Mark current plan step done and move to the next.",
      {{"type", "object"}, {"properties", json::object()}});
  add("plan_fail", "Mark the current plan step as failed.",
      {{"type", "object"}, {"properties", {{"note", {{"type", "string"}}}}}});
  add("plan_retry", "Clear failed flag on the current plan step and retry.",
      {{"type", "object"}, {"properties", json::object()}});
  add("plan_edit", "Edit one plan step text (default: current step).",
      {{"type", "object"},
       {"properties",
        {{"index", {{"type", "integer"}}}, {"text", {{"type", "string"}}}}},
       {"required", {"text"}}});
  add("remember", "Store a short fact in global memory (injected into future turns).",
      {{"type", "object"},
       {"properties", {{"text", {{"type", "string"}}}}},
       {"required", {"text"}}});
  add("forget", "Remove a memory entry by id or substring match on text.",
      {{"type", "object"},
       {"properties", {{"id", {{"type", "integer"}}}, {"text", {{"type", "string"}}}}}});
  add("list_memory", "List stored memory entries.", {{"type", "object"}, {"properties", json::object()}});
  add("ask_user",
      "Ask the user a question and wait for an answer. Use when a choice or missing detail blocks progress.",
      {{"type", "object"},
       {"properties",
        {{"question", {{"type", "string"}}},
         {"options", {{"type", "array"}, {"items", {{"type", "string"}}}}}}},
       {"required", {"question"}}});
  add("run_shell",
      "Read-only / build / test shell in workdir. Cannot write files (no >, >>, tee, sed -i) — use edit_file. "
      "rm asks when the delete toggle is on. Do not use & — long-running servers use start_background. "
      "ls and find hide node_modules and other ignored directories.",
      {{"type", "object"},
       {"properties", {{"command", {{"type", "string"}}}}},
       {"required", {"command"}}});
  add("start_background",
      "Start a long-running process in workdir (npm start, vite, a dev server). "
      "Returns immediately with a pid. The desk lists it and the user can stop it. Do not append &.",
      {{"type", "object"},
       {"properties", {{"command", {{"type", "string"}}}}},
       {"required", {"command"}}});
  tools_registry::append_openai_schemas(tools);
  return tools.dump();
}

}  // namespace arli
