#include "mutation_journal.hpp"
#include "atomic_publish.hpp"
#include "backup.hpp"
#include "file_io.hpp"
#include "private_state.hpp"
#include "mutation_catalog.hpp"
#include "workspace_identity.hpp"
#include <openssl/sha.h>
#include <sys/random.h>
#include <sys/file.h>
namespace arli::tools_internal {
using json=nlohmann::json;namespace fs=std::filesystem;
struct MutationJournalState {fs::path workdir,manifest;json record;bool poisoned=false,indexed=false;std::unique_ptr<FileFd> lease;};
namespace {
thread_local MutationJournalState* active=nullptr;
thread_local unsigned suspended=0;
struct Suspend {Suspend(){++suspended;}~Suspend(){--suspended;}};
void private_dir(const fs::path& path,bool strict=true){
  FileFd fd(open_parent(path/"entry",true));struct stat st{};
  if(fstat(fd.value,&st)||st.st_uid!=geteuid()||(st.st_mode&(strict?0077:0022)))throw std::runtime_error("MUTATION_JOURNAL: unsafe directory");
}
void save(MutationJournalState& state){
  Suspend suspension;const auto bytes=state.record.dump();
  if(bytes.size()>4*1024*1024)throw std::runtime_error("MUTATION_JOURNAL: manifest exceeds limit");
  const auto result=publish_file(state.manifest,bytes,fs::exists(state.manifest));
  if(!result.ok)throw std::runtime_error("MUTATION_JOURNAL: durable record unavailable");
}
void initialize(MutationJournalState& state){
  if(!state.manifest.empty())return;
  const auto root=fs::weakly_canonical(state_root());const auto project=root/"changesets"/mutation_hash(state.workdir.generic_string());
  private_dir(root,false);private_dir(root/"changesets");private_dir(project);
  unsigned char random[16];if(getrandom(random,sizeof(random),0)!=sizeof(random))throw std::runtime_error("MUTATION_JOURNAL: random ID unavailable");
  std::string id="turn-";for(auto byte:random){id+="0123456789abcdef"[byte>>4];id+="0123456789abcdef"[byte&15];}
  // Exclusive directory: a repeated dialog/turn attempt never overwrites its predecessor.
  FileFd parent(open_parent(project/"entry"));if(mkdirat(parent.value,id.c_str(),0700)||fsync(parent.value))throw std::runtime_error("MUTATION_JOURNAL: cannot allocate directory");
  state.lease=std::make_unique<FileFd>(open_parent(project/id/"entry"));
  if(flock(state.lease->value,LOCK_EX|LOCK_NB))throw std::runtime_error("MUTATION_JOURNAL: cannot acquire lease");
  state.manifest=project/id/"manifest.json";state.record["id"]=id;
}
void index(MutationJournalState& state){
  if(state.indexed)return;
  register_changeset(state.workdir,state.record["id"].get<std::string>(),state.record["dialog_id"].get<int64_t>(),state.record["turn_id"].get<int64_t>(),state.record["task_id"].get<int64_t>());state.indexed=true;
}
}
std::string mutation_hash(const std::string& bytes){
  unsigned char digest[SHA256_DIGEST_LENGTH];if(!SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),digest))throw std::runtime_error("mutation digest unavailable");
  std::string result;for(auto byte:digest){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
MutationScope::MutationScope(const fs::path& workdir,std::int64_t dialog,std::int64_t turn,std::int64_t task,const std::string& reverts)
    :state_(std::make_unique<MutationJournalState>()),previous_(active){
  state_->workdir=fs::weakly_canonical(workdir);
  state_->record={{"version",1},{"workdir",state_->workdir.generic_string()},{"dialog_id",dialog},{"turn_id",turn},{"task_id",task},{"entries",json::array()}};state_->record["workspace_identity"]=capture_workspace(state_->workdir);if(!reverts.empty())state_->record["reverts"]=reverts;active=state_.get();
}
MutationScope::~MutationScope(){active=previous_;}
fs::path MutationScope::path()const{std::error_code ec;return !state_->record["entries"].empty()&&fs::is_regular_file(state_->manifest,ec)?state_->manifest:fs::path{};}
void MutationScope::record_acceptance(const json& plan){
  try{
    if(state_->poisoned)throw std::runtime_error("ACCEPTANCE_STORE: previous record failed");
    if(state_->record.contains("acceptance_plan")){
      if(state_->record["acceptance_plan"]!=plan)throw std::runtime_error("ACCEPTANCE_STORE: criteria are immutable");
      return;
    }
    if(!state_->record["entries"].empty()||!plan.is_object()||plan.dump().size()>65536)throw std::runtime_error("ACCEPTANCE_STORE: invalid or late criteria");
    Suspend suspension;initialize(*state_);state_->record["acceptance_plan"]=plan;
    state_->record["acceptance_hash"]=mutation_hash(plan.dump());save(*state_);index(*state_);
  }catch(...){state_->poisoned=true;throw;}
}
MutationTicket prepare_mutation(const fs::path& input,const std::string& after,bool replace,const std::string* expected,bool after_exists){
  if(!active||suspended)return {};
  const auto file=fs::absolute(input).lexically_normal();
  if(!path_within(active->workdir,file)||private_state_path(file))return {};
  if(active->poisoned)throw std::runtime_error("MUTATION_JOURNAL: previous record failed; stop this turn");
  if(active->record["entries"].size()>=4096)throw std::runtime_error("MUTATION_JOURNAL: entry limit");
  Suspend suspension;MutationTicket ticket{active,active->record["entries"].size(),replace?read_file_bytes(file):std::string()};
  if(expected&&ticket.before!=*expected)throw std::runtime_error("FILE_CHANGED: source differs before journal preparation");
  initialize(*active);std::string snapshot;
  if(replace){const auto saved=backup_regular_file(active->workdir,file,&ticket.before,false);if(!saved.ok)throw std::runtime_error("MUTATION_JOURNAL: before snapshot failed");snapshot=fs::path(saved.output).lexically_relative(fs::weakly_canonical(state_root())).generic_string();}
  active->record["entries"].push_back({{"sequence",ticket.entry+1},{"path",file.lexically_relative(active->workdir).generic_string()},
    {"operation",!after_exists?"delete":replace?"replace":"create"},{"before_exists",replace},{"before_hash",replace?mutation_hash(ticket.before):""},
    {"before_snapshot",snapshot},{"after_exists",after_exists},{"after_hash",after_exists?mutation_hash(after):""},{"phase","prepared"}});
  try{save(*active);index(*active);}catch(...){active->poisoned=true;throw;}return ticket;
}
ToolResult finish_mutation(const MutationTicket& ticket,ToolResult result,bool published){
  if(!ticket.state)return result;
  auto& state=*ticket.state;
  try{
    state.record["entries"][ticket.entry]["phase"]=result.ok?"applied":published?"uncertain":"failed";
    save(state);
  }catch(...){state.poisoned=true;return {false,"MUTATION_UNCERTAIN: publication outcome could not be durably recorded; durable intent retained"};}
  return result;
}
}
