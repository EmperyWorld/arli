#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

// gRPC transport/auth for Arli loopback:
//   transport — grpc local credentials (LOCAL_TCP): peer must be loopback, no TLS files;
//   identity  — metadata `authorization: Bearer <token>`, checked by one server-side
//               AuthMetadataProcessor per service (Agent → ui_token, AgentHub → internal_token).
// Health is open (no token) so the shell can show «нет демона» before reading secrets.
namespace arli::auth {

inline constexpr const char* kHeader = "authorization";
inline constexpr const char* kWorkerLaunchHeader = "x-arli-worker-launch";

std::shared_ptr<grpc::ServerCredentials> server_credentials(std::string ui_token, std::string internal_token);

std::shared_ptr<grpc::ChannelCredentials> client_credentials();
std::shared_ptr<grpc::Channel> make_bounded_channel(const std::string& endpoint, int max_bytes);
std::shared_ptr<grpc::Channel> make_channel(const std::string& endpoint);

// Attach bearer token to an outgoing call. Empty token → no header.
void worker_launch(grpc::ClientContext& ctx, const std::string& token);
void bearer(grpc::ClientContext& ctx, const std::string& token);

// Which token a method path requires. Returns "ui" | "internal" | "" (open).
const char* required_role(const std::string& method_path);

}  // namespace arli::auth
