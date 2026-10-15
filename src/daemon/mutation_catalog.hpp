#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
namespace arli {
// Worker writes a durable locator before workspace publication. Daemon only reads metadata.
void register_changeset(const std::filesystem::path& workdir,const std::string& id,int64_t dialog,int64_t turn,int64_t task);
struct TaskJournal {std::string id;bool has_mutations=false;nlohmann::json acceptance;};
std::vector<TaskJournal> task_journals(const std::filesystem::path& workdir,int64_t dialog,int64_t turn,int64_t task);
std::vector<std::string> task_changesets(const std::filesystem::path& workdir,int64_t dialog,int64_t turn,int64_t task);
}
