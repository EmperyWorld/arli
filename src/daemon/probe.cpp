#include "auth.hpp"
#include "paths.hpp"
#include "secrets.hpp"

#include "arli.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// arli-probe [endpoint] [--auth]
//   default: Health only (open RPC) → prints version/slice.
//   --auth : also checks that ListDialogs is UNAUTHENTICATED without token and OK with ui_token.
int main(int argc, char** argv) {
  std::string endpoint = arli::kListenEndpoint;
  bool check_auth = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("arli-probe %s\n", arli::kVersion);
      return 0;
    }
    if (std::strcmp(argv[i], "--auth") == 0) {
      check_auth = true;
    } else if (argv[i] && *argv[i]) {
      endpoint = argv[i];
    }
  }
  auto channel = arli::auth::make_channel(endpoint);
  auto stub = arli::v1::Agent::NewStub(channel);
  {
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    arli::v1::HealthRequest req;
    arli::v1::HealthReply reply;
    const grpc::Status st = stub->Health(&ctx, req, &reply);
    if (!st.ok()) {
      std::fprintf(stderr, "health fail: %s\n", st.error_message().c_str());
      return 1;
    }
    std::printf("ok serving=%d slice=%s version=%s\n", reply.serving() ? 1 : 0, reply.slice().c_str(),
                reply.version().c_str());
    if (reply.slice() != arli::kSlice) {
      std::fprintf(stderr, "slice mismatch: probe %s daemon %s\n", arli::kSlice, reply.slice().c_str());
      return 2;
    }
  }
  if (!check_auth) {
    return 0;
  }
  {
    grpc::ClientContext ctx;
    arli::v1::ListDialogsRequest req;
    arli::v1::ListDialogsReply reply;
    const grpc::Status st = stub->ListDialogs(&ctx, req, &reply);
    if (st.error_code() != grpc::StatusCode::UNAUTHENTICATED) {
      std::fprintf(stderr, "AUTH FAIL: ListDialogs without token → %d (%s), want UNAUTHENTICATED\n",
                   static_cast<int>(st.error_code()), st.error_message().c_str());
      return 3;
    }
    std::printf("ok no-token → UNAUTHENTICATED\n");
  }
  {
    grpc::ClientContext ctx;
    arli::auth::bearer(ctx, "not-the-token");
    arli::v1::ListDialogsRequest req;
    arli::v1::ListDialogsReply reply;
    const grpc::Status st = stub->ListDialogs(&ctx, req, &reply);
    if (st.error_code() != grpc::StatusCode::UNAUTHENTICATED) {
      std::fprintf(stderr, "AUTH FAIL: bad token accepted\n");
      return 4;
    }
    std::printf("ok bad-token → UNAUTHENTICATED\n");
  }
  {
    const std::string token = arli::secrets::ui_token();
    if (token.empty()) {
      std::fprintf(stderr, "no ui token at %s\n", arli::secrets::ui_token_path().c_str());
      return 5;
    }
    grpc::ClientContext ctx;
    arli::auth::bearer(ctx, token);
    arli::v1::ListDialogsRequest req;
    arli::v1::ListDialogsReply reply;
    const grpc::Status st = stub->ListDialogs(&ctx, req, &reply);
    if (!st.ok()) {
      std::fprintf(stderr, "AUTH FAIL: ui token rejected: %s\n", st.error_message().c_str());
      return 6;
    }
    std::printf("ok ui-token → %d dialogs\n", reply.dialogs_size());
  }
  {
    // Worker service must not accept the UI token.
    auto hub = arli::v1::AgentHub::NewStub(channel);
    grpc::ClientContext ctx;
    arli::auth::bearer(ctx, arli::secrets::ui_token());
    auto stream = hub->Session(&ctx);
    arli::v1::OrchToWorker in;
    stream->WritesDone();
    while (stream->Read(&in)) {
    }
    const grpc::Status st = stream->Finish();
    if (st.error_code() != grpc::StatusCode::UNAUTHENTICATED) {
      std::fprintf(stderr, "AUTH FAIL: AgentHub.Session with ui token → %d, want UNAUTHENTICATED\n",
                   static_cast<int>(st.error_code()));
      return 7;
    }
    std::printf("ok ui-token on AgentHub → UNAUTHENTICATED\n");
  }
  std::printf("AUTH_OK\n");
  return 0;
}
