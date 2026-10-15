#pragma once
#include "grpc_tool_provider.hpp"
#include <memory>
namespace arli {
// Trusted operator configuration only: state/grpc-tools/registry.json.
// Missing directory disables tools; any present but invalid registry fails closed.
std::unique_ptr<GrpcToolProvider> load_grpc_registry();
}
