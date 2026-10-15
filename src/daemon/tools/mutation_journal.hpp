#pragma once
#include "../tools.hpp"
#include <memory>
#include <cstdint>
namespace arli::tools_internal {
struct MutationJournalState;
// One worker-thread scope per run_turn; file publication records durable intent.
class MutationScope {
 public:
  MutationScope(const std::filesystem::path& workdir,std::int64_t dialog,std::int64_t turn,std::int64_t task=0,const std::string& reverts={});
  ~MutationScope();
  MutationScope(const MutationScope&)=delete;
  MutationScope& operator=(const MutationScope&)=delete;
  std::filesystem::path path() const;
  // Immutable criteria, fsynced and indexed before any mutating dispatch.
  void record_acceptance(const nlohmann::json& plan);
 private:
  std::unique_ptr<MutationJournalState> state_;
  MutationJournalState* previous_=nullptr;
};
struct MutationTicket {MutationJournalState* state=nullptr;size_t entry=0;std::string before;};
MutationTicket prepare_mutation(const std::filesystem::path& file,const std::string& after,bool replace,const std::string* expected,bool after_exists=true);
ToolResult finish_mutation(const MutationTicket& ticket,ToolResult result,bool published);
std::string mutation_hash(const std::string& bytes);
}
