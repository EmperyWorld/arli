#include "backup_store.hpp"
#include "file_io.hpp"
#include "paths.hpp"
#include <openssl/sha.h>
namespace arli::tools_internal {
namespace {
std::string digest(const std::string& value){
  unsigned char bytes[SHA256_DIGEST_LENGTH];if(!SHA256(reinterpret_cast<const unsigned char*>(value.data()),value.size(),bytes))throw std::runtime_error("backup namespace unavailable");
  const char* hex="0123456789abcdef";std::string out;for(auto byte:bytes){out+=hex[byte>>4];out+=hex[byte&15];}return out;
}
void private_directory(const std::filesystem::path& path,bool create,bool private_only=true){
  FileFd dir(open_parent(path/"entry",create));struct stat st{};
  if(fstat(dir.value,&st)||st.st_uid!=geteuid()||(st.st_mode&(private_only?0077:0022)))throw std::runtime_error("backup storage must be private and owned by current user");
}
}
std::filesystem::path backup_path(const std::filesystem::path& workdir,const std::filesystem::path& file){
  const auto root=std::filesystem::weakly_canonical(workdir);
  const auto rel=std::filesystem::absolute(file).lexically_normal().lexically_relative(root);
  if(rel.empty()||rel=="."||rel.is_absolute())throw std::runtime_error("invalid backup source path");
  for(const auto& part:rel)if(part=="..")throw std::runtime_error("backup source outside workdir");
  return std::filesystem::weakly_canonical(state_root())/"backups"/digest(root.generic_string())/digest(rel.generic_string())/"latest";
}
void prepare_backup_store(const std::filesystem::path& workdir,const std::filesystem::path& file,bool create){
  const auto path=backup_path(workdir,file);const auto state=std::filesystem::weakly_canonical(state_root());
  private_directory(state,create,false);private_directory(state/"backups",create);
  private_directory(path.parent_path().parent_path(),create);private_directory(path.parent_path(),create);
}
}
