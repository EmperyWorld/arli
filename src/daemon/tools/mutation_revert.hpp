#pragma once
#include "../tools.hpp"
#include <cstdint>
#include <functional>
namespace arli::tools_internal {
// Worker-only; caller must authorize deletion and own the dialog/task.
// Conflicts are retained; returns per-file outcomes and a new undo journal.
nlohmann::json revert_mutations(const std::filesystem::path& workdir,const std::string& id,
                               int64_t dialog,int64_t turn,int64_t task=0,const std::function<bool()>& cancelled={});
}
