#include "auth.hpp"
#include "paths.hpp"
#include "secrets.hpp"

#include "arli.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
  std::string endpoint = arli::kListenEndpoint;
  std::string text = "скажи одним словом: пинг";
  if (argc >= 2 && argv[1] && *argv[1]) {
    endpoint = argv[1];
  }
  if (argc >= 3 && argv[2] && *argv[2]) {
    text = argv[2];
  }

  auto channel = arli::auth::make_channel(endpoint);
  auto stub = arli::v1::Agent::NewStub(channel);

  {
    grpc::ClientContext ctx;
    arli::auth::bearer(ctx, arli::secrets::ui_token());
    arli::v1::HealthRequest req;
    arli::v1::HealthReply reply;
    const auto st = stub->Health(&ctx, req, &reply);
    if (!st.ok() || !reply.serving()) {
      std::fprintf(stderr, "health fail\n");
      return 1;
    }
  }

  {
    grpc::ClientContext ctx;
    arli::auth::bearer(ctx, arli::secrets::ui_token());
    arli::v1::Settings req;
    req.set_llm_base_url("http://127.0.0.1:8080/v1");
    req.set_llm_model("/models/gemma-4-26B.gguf");
    req.set_workdir((arli::state_root() / "workspace").string());
    arli::v1::Settings reply;
    const auto st = stub->SetSettings(&ctx, req, &reply);
    if (!st.ok()) {
      std::fprintf(stderr, "settings fail: %s\n", st.error_message().c_str());
      return 1;
    }
  }

  std::int64_t dialog_id = 0;
  {
    grpc::ClientContext ctx;
    arli::auth::bearer(ctx, arli::secrets::ui_token());
    arli::v1::CreateDialogRequest req;
    req.set_title("smoke");
    arli::v1::Dialog reply;
    const auto st = stub->CreateDialog(&ctx, req, &reply);
    if (!st.ok()) {
      std::fprintf(stderr, "create fail: %s\n", st.error_message().c_str());
      return 1;
    }
    dialog_id = reply.id();
  }

  grpc::ClientContext ctx;

  arli::auth::bearer(ctx, arli::secrets::ui_token());
  arli::v1::SendRequest req;
  req.set_dialog_id(dialog_id);
  req.set_text(text);
  std::unique_ptr<grpc::ClientReader<arli::v1::TurnEvent>> reader(stub->Send(&ctx, req));
  arli::v1::TurnEvent ev;
  bool got_done = false;
  while (reader->Read(&ev)) {
    if (ev.has_token()) {
      std::printf("token: %s\n", ev.token().text().c_str());
    } else if (ev.has_thought()) {
      std::printf("thought: %s\n", ev.thought().text().c_str());
    } else if (ev.has_action()) {
      std::printf("action: %s\n", ev.action().tool().c_str());
    } else if (ev.has_observation()) {
      std::printf("obs ok=%d\n", ev.observation().ok() ? 1 : 0);
    } else if (ev.has_error()) {
      std::printf("error: %s\n", ev.error().message().c_str());
    } else if (ev.has_done()) {
      std::printf("done: %s\n", ev.done().assistant_text().c_str());
      got_done = true;
    } else if (ev.has_status()) {
      std::printf("status: %s\n", ev.status().text().c_str());
    }
  }
  const auto fin = reader->Finish();
  if (!fin.ok()) {
    std::fprintf(stderr, "send finish: %s\n", fin.error_message().c_str());
    return 1;
  }
  if (!got_done) {
    std::fprintf(stderr, "no done event\n");
    return 1;
  }
  std::printf("SMOKE_OK dialog=%lld\n", static_cast<long long>(dialog_id));
  return 0;
}
