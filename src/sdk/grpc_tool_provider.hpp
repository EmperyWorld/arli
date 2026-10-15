#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
namespace arli {
struct GrpcToolMethod {
  std::string tool_name; // Reserved namespace: grpc_<name>.
  std::string endpoint; // Numeric loopback only, fixed by trusted host configuration.
  std::string method; // Fully qualified protobuf method, e.g. package.Service.Method.
  std::string descriptor_set; // Serialized FileDescriptorSet including imports; owned by provider.
  std::string bearer_token; // Host-supplied service credential; never part of tool schema/results.
  bool mutating = false;
  int deadline_ms = 5000;
};
struct GrpcToolResult { bool ok = false; std::string output; };
class GrpcToolProvider {
 public:
  GrpcToolProvider();
  ~GrpcToolProvider();
  // Configure before using the provider; duplicate/invalid/streaming methods fail closed.
  void add(GrpcToolMethod method);
  bool contains(const std::string& name) const;
  bool mutating(const std::string& name) const;
  std::string schemas_json() const;
  // Approval receives the tool name and canonical protobuf JSON, never credentials.
  GrpcToolResult invoke(const std::string& name, const std::string& arguments, bool read_only,
      std::atomic<bool>* cancel, const std::function<bool(const std::string&)>& approve = {}) const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
