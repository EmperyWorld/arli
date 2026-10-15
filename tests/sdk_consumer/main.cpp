#include <arli/auth.hpp>
#include <arli/grpc_tool_provider.hpp>
#include <google/protobuf/descriptor.pb.h>
#include <arli/arli.grpc.pb.h>
#include <chrono>
#ifdef TEST_EMBED
#include <arli/embed.hpp>
#include <QCoreApplication>
#endif
class Fixture final : public arli::v1::Agent::Service {
  grpc::Status ListDialogs(grpc::ServerContext*, const arli::v1::ListDialogsRequest*, arli::v1::ListDialogsReply* reply) override {
    reply->add_dialogs()->set_title("installed SDK"); return grpc::Status::OK;
  }
};
int main(int argc, char** argv) {
#ifdef TEST_EMBED
  QCoreApplication app(argc, argv);
  AgentFacade facade;
  if (std::string(facade.metaObject()->className()) != "AgentFacade" || arli::embed::qmlImportPath().isEmpty()) return 1;
#endif
  Fixture service; grpc::ServerBuilder builder; int port = 0;
  builder.AddListeningPort("127.0.0.1:0", arli::auth::server_credentials("fixture-ui", "fixture-worker"), &port);
  builder.RegisterService(&service); auto server = builder.BuildAndStart(); if (!server || !port) return 2;
  auto stub = arli::v1::Agent::NewStub(arli::auth::make_channel("127.0.0.1:" + std::to_string(port)));
  google::protobuf::FileDescriptorSet descriptors;
  arli::v1::ListDialogsRequest::descriptor()->file()->CopyTo(descriptors.add_file());
  arli::GrpcToolProvider provider;
  provider.add({"grpc_dialogs", "127.0.0.1:" + std::to_string(port), "arli.v1.Agent.ListDialogs",
      descriptors.SerializeAsString(), "fixture-ui", false, 1000});
  const auto result = provider.invoke("grpc_dialogs", "{}", true, nullptr);
  bool passed = result.ok && result.output.find("installed SDK") != std::string::npos;
  for (bool authorized : {false, true}) {
    grpc::ClientContext ctx; ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(2));
    arli::auth::bearer(ctx, authorized ? "fixture-ui" : "wrong");
    arli::v1::ListDialogsReply reply;
    const auto status = stub->ListDialogs(&ctx, arli::v1::ListDialogsRequest{}, &reply);
    passed &= authorized ? status.ok() && reply.dialogs_size()==1 && reply.dialogs(0).title()=="installed SDK"
                         : status.error_code()==grpc::StatusCode::UNAUTHENTICATED;
  }
  server->Shutdown(); return passed ? 0 : 3;
}
