#pragma once
#include <filesystem>
namespace arli::tools_internal {
// Stable project/path namespaces. Calculation alone does not create storage.
std::filesystem::path backup_path(const std::filesystem::path& workdir,const std::filesystem::path& file);
void prepare_backup_store(const std::filesystem::path& workdir,const std::filesystem::path& file,bool create);
}
