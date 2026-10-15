#pragma once
#include "db.hpp"
#include <dirent.h>
#include <filesystem>
namespace arli {
// Bounded directory scan; registered jobs belong to normal lifecycle/retention.
class OrphanJobs {
 public:
  OrphanJobs()=default;
  OrphanJobs(const OrphanJobs&)=delete;
  OrphanJobs& operator=(const OrphanJobs&)=delete;
  ~OrphanJobs(){if(entries_)closedir(entries_);}
  void sweep(Database& db);
 private:
  DIR* entries_=nullptr;
  std::filesystem::path root_;
};
}
