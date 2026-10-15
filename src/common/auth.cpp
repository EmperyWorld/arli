#include "auth.hpp"

#include "secrets.hpp"
#include "redact.hpp"

#include <grpc/grpc_security_constants.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/auth_metadata_processor.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>

#include <string_view>
#include <atomic>

namespace arli::auth {
namespace {

constexpr std::string_view kBearer = "Bearer ";

class TokenProcessor final : public grpc::AuthMetadataProcessor {
 public:
  TokenProcessor(std::string ui, std::string internal)
      : ui_(std::move(ui)), internal_(std::move(internal)) {
    // Publish the tokens explicitly.  The distro gRPC library is not TSan
    // instrumented, so its worker-thread hand-off is otherwise invisible to
    // the race detector.
    published_.store(true, std::memory_order_release);
  }

  bool IsBlocking() const override { return false; }

  grpc::Status Process(const InputMetadata& auth_metadata, grpc::AuthContext* /*context*/,
                       OutputMetadata* /*consumed*/, OutputMetadata* /*response*/) override {
    if (!published_.load(std::memory_order_acquire)) {
      return grpc::Status(grpc::StatusCode::UNAVAILABLE, "authentication is not initialized");
    }
    std::string path;
    if (auto it = auth_metadata.find(":path"); it != auth_metadata.end()) {
      path.assign(it->second.data(), it->second.size());
    }
    const char* role = required_role(path);
    if (!*role) {
      return grpc::Status::OK;
    }
    const std::string& expected = role[0] == 'u' ? ui_ : internal_;
    if (expected.empty()) {
      return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "server has no token configured");
    }
    auto it = auth_metadata.find(kHeader);
    if (it == auth_metadata.end()) {
      return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "missing authorization");
    }
    std::string_view value(it->second.data(), it->second.size());
    if (value.size() <= kBearer.size() || value.substr(0, kBearer.size()) != kBearer) {
      return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "bad authorization scheme");
    }
    if (!secrets::constant_time_equal(value.substr(kBearer.size()), expected)) {
      return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "bad token");
    }
    return grpc::Status::OK;
  }

 private:
  std::string ui_;
  std::string internal_;
  std::atomic<bool> published_{false};
};

}  // namespace

const char* required_role(const std::string& method_path) {
  if (method_path == "/arli.v1.Agent/Health") {
    return "";
  }
  if (method_path.rfind("/arli.v1.AgentHub/", 0) == 0) {
    return "internal";
  }
  if (method_path.rfind("/arli.v1.Agent/", 0) == 0) {
    return "ui";
  }
  // Unknown service: require ui token (fail closed).
  return "ui";
}

std::shared_ptr<grpc::ServerCredentials> server_credentials(std::string ui_token, std::string internal_token) {
  redact::remember(ui_token);redact::remember(internal_token);
  auto creds = grpc::experimental::LocalServerCredentials(LOCAL_TCP);
  creds->SetAuthMetadataProcessor(
      std::make_shared<TokenProcessor>(std::move(ui_token), std::move(internal_token)));
  return creds;
}

std::shared_ptr<grpc::ChannelCredentials> client_credentials() {
  return grpc::experimental::LocalCredentials(LOCAL_TCP);
}

std::shared_ptr<grpc::Channel> make_channel(const std::string& endpoint) {
  return make_bounded_channel(endpoint, 4 * 1024 * 1024);
}

std::shared_ptr<grpc::Channel> make_bounded_channel(const std::string& endpoint, int max_bytes) {
  // Loopback: reconnect quickly after a daemon restart instead of gRPC's 1 s → 120 s backoff.
  grpc::ChannelArguments args;
  args.SetMaxReceiveMessageSize(max_bytes);
  args.SetMaxSendMessageSize(max_bytes);
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 100);
  args.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, 100);
  args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 1000);
  return grpc::CreateCustomChannel(endpoint, client_credentials(), args);
}

void worker_launch(grpc::ClientContext& ctx, const std::string& token) {
  redact::remember(token);
  if (!token.empty()) ctx.AddMetadata(kWorkerLaunchHeader, token);
}

void bearer(grpc::ClientContext& ctx, const std::string& token) {
  if (token.empty()) {
    return;
  }
  redact::remember(token);
  ctx.AddMetadata(kHeader, std::string(kBearer) + token);
}

}  // namespace arli::auth
