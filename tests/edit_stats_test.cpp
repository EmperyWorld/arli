#include "edit_stats.hpp"

#include <iostream>

static int fail(const std::string& msg) {
  std::cerr << "FAIL " << msg << "\n";
  return 1;
}

int main() {
  arli::TurnEditStats stats;
  arli::note_mutation_tool(stats, "append_file", R"({"path":"a.js","content":"line1\nline2\nline3"})");
  if (stats.lines_added < 3 || stats.files_touched() != 1) {
    return fail("append");
  }
  arli::note_mutation_tool(stats, "edit_file",
                           R"({"path":"a.js","start_line":1,"end_line":2,"new_text":"x\ny\nz\nw"})");
  if (stats.lines_removed < 2) {
    return fail("edit removed");
  }
  arli::v1::Done done;
  arli::fill_done_edit_stats(stats, &done);
  if (!done.has_edit_stats() || done.edit_stats().files_touched() != 1) {
    return fail("proto fill");
  }
  arli::TurnEditStats strict;
  arli::note_mutation_tool(strict, "replace_text", R"({"path":"b.js","old_text":"old\nlines\n","new_text":"new\n"})");
  if (strict.lines_removed != 2 || strict.lines_added != 1 || strict.files_touched() != 1)
    return fail("strict replacement counts actual source lines without phantom EOF line");
  arli::note_mutation_tool(strict, "edit_file", R"({"path":"b.js","start_line":1,"end_line":1,"text":"x\ny"})");
  if (strict.lines_added != 3) return fail("line tool records canonical text argument");
  arli::TurnEditStats batch;
  arli::note_mutation_tool(batch,"batch_replace",R"({"edits":[{"path":"a.js","old_text":"a","new_text":"b"},{"path":"b.js","old_text":"x\ny","new_text":"z"}]})");
  if(batch.files_touched()!=2||batch.lines_removed!=3||batch.lines_added!=2)return fail("batch replacement stats");
  std::cout << "OK edit-stats\n";
  return 0;
}
