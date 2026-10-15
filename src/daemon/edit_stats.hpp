#pragma once

#include "arli.grpc.pb.h"

#include <set>
#include <string>

namespace arli {

struct TurnEditStats {
  int lines_added = 0;
  int lines_removed = 0;
  std::set<std::string> files;

  int files_touched() const { return static_cast<int>(files.size()); }
  bool empty() const { return lines_added == 0 && lines_removed == 0 && files.empty(); }
};

void note_mutation_tool(TurnEditStats& stats, const std::string& tool, const std::string& args_json);

void fill_done_edit_stats(const TurnEditStats& stats, v1::Done* done);

}  // namespace arli
