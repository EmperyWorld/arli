#include "paths.hpp"
#include "server.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
  if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("arli-daemon %s\n", arli::kVersion);
    return 0;
  }
  std::string endpoint = arli::kListenEndpoint;
  if (const char* env = std::getenv("ARLI_LISTEN"); env && *env) {
    endpoint = env;
  }
  if (argc >= 2 && argv[1] && *argv[1]) {
    endpoint = argv[1];
  }
  if (!arli::listen_endpoint_allowed(endpoint)) {
    std::fprintf(stderr, "arli-daemon: listen refused (only 127.0.0.1 / localhost)\n");
    return 2;
  }
  const auto db = arli::db_path().string();
  std::fprintf(stderr, "arli-daemon: %s db=%s\n", endpoint.c_str(), db.c_str());
  return arli::serve(endpoint, db);
}
