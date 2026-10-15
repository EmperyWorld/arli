#pragma once
#include "../tools.hpp"
#include <functional>
namespace arli::tools_internal {
// Read-only evidence, not authorization to overwrite. Revalidate at mutation time.
// Rejects active journals; never changes original phases after a crash.
nlohmann::json inspect_mutations(const std::filesystem::path& workdir,const std::string& changeset_id);
// Holds an exclusive original-journal lease throughout the callback.
void locked_mutations(const std::filesystem::path&,const std::string&,const std::function<void(const nlohmann::json&)>&);
}
