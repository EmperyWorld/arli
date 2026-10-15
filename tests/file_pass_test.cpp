#include "file_pass.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

static int fail(const std::string& msg) {
  std::cerr << "FAIL " << msg << "\n";
  return 1;
}

int main() {
  arli::FilePassRow row;
  row.paths = {"a.html", "b.css", "c.js"};
  arli::FilePassState pass = arli::FilePassState::from_row(row);
  if (pass.head() != "a.html") {
    return fail("head");
  }
  pass.on_read_ok("a.html");
  if (!pass.hold_for_write()) {
    return fail("hold after read");
  }
  const auto blocked = arli::file_pass_gate_tool(pass, "read_file", R"({"path":"b.css"})");
  if (!blocked.ok) {
    return fail("dependency read must be allowed");
  }
  pass.on_mutation_ok("a.html");
  if (pass.hold_for_write()) {
    return fail("hold cleared after write");
  }
  if (pass.head() != "b.css") {
    return fail("head advances");
  }
  if (!pass.is_done("a.html")) {
    return fail("a marked done");
  }

  nlohmann::json set = {{"paths", nlohmann::json::array({"x.txt", "y.txt"})}};
  if (!pass.set_paths(set).ok) {
    return fail("set_paths");
  }
  if (pass.head() != "x.txt") {
    return fail("reset head");
  }

  if (pass.review_without_changes({{"path", "x.txt"}, {"note", "already correct"}}).ok)
    return fail("review without read");
  pass.on_read_ok("x.txt");
  if (pass.review_without_changes({{"path", "x.txt"}, {"note", " "}}).ok) return fail("empty reason");
  if (!pass.review_without_changes({{"path", "x.txt"}, {"note", "already correct"}}).ok)
    return fail("review after read");
  arli::v1::FilePassSnapshot snapshot;
  pass.fill_snapshot(&snapshot);
  if (snapshot.done_size() != 0 || snapshot.reviewed_size() != 1 || pass.head() != "y.txt")
    return fail("review must not claim a write");
  arli::FilePassRow restored;
  restored.paths = {"x.txt", "y.txt"};
  restored.reviewed = {"x.txt"};
  if (arli::FilePassState::from_row(restored).head() != "y.txt") return fail("restore reviewed state");

  arli::FilePassState dependent;
  if (!dependent.set_paths({{"paths", {"a.txt", "b.txt"}}}).ok) return fail("dependency queue");
  dependent.on_read_ok("a.txt");
  dependent.on_read_ok("b.txt");
  dependent.on_mutation_ok("b.txt");
  if (!dependent.is_done("b.txt") || dependent.head() != "a.txt" || !dependent.hold_for_write())
    return fail("out-of-order dependency mutation");
  if (!dependent.review_without_changes({{"path", "a.txt"}, {"note", "correct"}}).ok || !dependent.head().empty())
    return fail("dependency queue completion");

  std::cout << "OK file-pass\n";
  return 0;
}
