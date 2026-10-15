#include "auth.hpp"
#include "paths.hpp"
#include "secrets.hpp"

#include "arli.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int fail(const char* msg) {
  std::fprintf(stderr, "FAIL %s\n", msg);
  return 1;
}

int main(int argc, char** argv) {
  const char* ep = argc >= 2 ? argv[1] : "127.0.0.1:4795";
  auto channel = arli::auth::make_channel(ep);
  std::unique_ptr<arli::v1::Agent::Stub> stub = arli::v1::Agent::NewStub(channel);

  {
    grpc::ClientContext ctx;
    arli::auth::bearer(ctx, arli::secrets::ui_token());
    arli::v1::HealthReply hr;
    auto st = stub->Health(&ctx, {}, &hr);
    if (!st.ok() || !hr.serving()) return fail("health");
    if (hr.slice() != arli::kSlice) {
      std::fprintf(stderr, "FAIL slice want %s got %s\n", arli::kSlice, hr.slice().c_str());
      return 1;
    }
    std::printf("OK health slice=%s\n", hr.slice().c_str());
  }

  std::int64_t dialog_id = 0;
  {
    grpc::ClientContext ctx;
    arli::auth::bearer(ctx, arli::secrets::ui_token());
    arli::v1::CreateDialogRequest req;
    req.set_title("orch-smoke");
    arli::v1::Dialog d;
    auto st = stub->CreateDialog(&ctx, req, &d);
    if (!st.ok()) return fail("create dialog");
    dialog_id = d.id();
  }

  bool saw_status = false;
  bool saw_done_or_error = false;
  {
    grpc::ClientContext ctx;
    arli::auth::bearer(ctx, arli::secrets::ui_token());
    arli::v1::SendRequest req;
    req.set_dialog_id(dialog_id);
    req.set_text("скажи одно слово: ок");
    std::unique_ptr<grpc::ClientReader<arli::v1::TurnEvent>> reader(stub->Send(&ctx, req));
    arli::v1::TurnEvent ev;
    while (reader->Read(&ev)) {
      if (ev.has_status()) {
        saw_status = true;
        std::printf("status: %s\n", ev.status().text().c_str());
      }
      if (ev.has_thought()) {
        std::printf("thought: %zu chars\n", ev.thought().text().size());
      }
      if (ev.has_token()) {
        std::printf("token: %s\n", ev.token().text().substr(0, 80).c_str());
      }
      if (ev.has_done()) {
        saw_done_or_error = true;
        std::printf("done: %s\n", ev.done().assistant_text().substr(0, 120).c_str());
      }
      if (ev.has_error()) {
        saw_done_or_error = true;
        std::printf("error: %s\n", ev.error().message().substr(0, 160).c_str());
      }
    }
    auto st = reader->Finish();
    if (!st.ok()) {
      std::fprintf(stderr, "FAIL send status: %s\n", st.error_message().c_str());
      return 1;
    }
  }

  if (!saw_done_or_error) return fail("no done/error from worker path");
  std::printf("ORCH_OK claim_path dialog=%lld status=%d\n", static_cast<long long>(dialog_id),
              saw_status ? 1 : 0);
  return 0;
}
