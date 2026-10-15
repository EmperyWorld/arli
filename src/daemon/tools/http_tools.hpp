#pragma once

#include "tools.hpp"

#include <string>
#include <vector>

namespace arli {

ToolResult tool_http_get(const std::string& url);

ToolResult tool_http_request(const std::string& method, const std::string& url, const std::string& body);

// Pure validation seam for dual-stack/DNS-rebinding regression tests.
bool pin_public_addresses(const std::string& host, const std::string& port,
                          const std::vector<std::string>& addresses,
                          std::string& pinned, std::string& why);

}  // namespace arli
