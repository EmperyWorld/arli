#include "memory.hpp"

#include <iostream>

static int fail(const std::string& msg) {
  std::cerr << "FAIL " << msg << "\n";
  return 1;
}

int main() {
  arli::MemoryState mem = arli::MemoryState::load({});
  if (!mem.inject_block().empty()) {
    return fail("empty inject");
  }

  nlohmann::json j = {{"text", "любит зелёный чай"}};
  if (!mem.remember(j).ok) {
    return fail("remember");
  }
  const std::string block = mem.inject_block();
  if (block.find("зелёный чай") == std::string::npos) {
    return fail("inject contains text");
  }

  for (int i = 0; i < arli::kMemoryStoreLimit + 5; ++i) {
    mem.remember({{"text", "item-" + std::to_string(i)}});
  }
  if (static_cast<int>(mem.entries().size()) > arli::kMemoryStoreLimit) {
    return fail("store cap");
  }

  if (!mem.list().ok) {
    return fail("list");
  }

  std::cout << "OK memory-store\n";
  return 0;
}
