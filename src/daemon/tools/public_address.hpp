#pragma once
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <string>

namespace arli {
inline bool public_ipv4(const in_addr& address) {
  const uint32_t ip = ntohl(address.s_addr);
  const unsigned a = ip >> 24, b = (ip >> 16) & 255;
  return a != 0 && a != 10 && a != 127 && a < 224 &&
      !(a == 100 && b >= 64 && b <= 127) && !(a == 169 && b == 254) &&
      !(a == 172 && b >= 16 && b <= 31) && !(a == 192 && (b == 168 || b == 0)) &&
      !(a == 198 && (b == 18 || b == 19 || b == 51)) &&
      !(a == 203 && b == 0 && ((ip >> 8) & 255) == 113);
}
inline bool public_address(const std::string& value) {
  in_addr v4{};
  if (inet_pton(AF_INET, value.c_str(), &v4) == 1) return public_ipv4(v4);
  in6_addr v6{};
  if (inet_pton(AF_INET6, value.c_str(), &v6) != 1) return false;
  if (IN6_IS_ADDR_V4MAPPED(&v6)) {
    std::memcpy(&v4, v6.s6_addr + 12, sizeof(v4));
    return public_ipv4(v4);
  }
  // Accept global unicast only; exclude transition and documentation ranges.
  if ((v6.s6_addr[0] & 0xe0) != 0x20) return false;
  if (v6.s6_addr[0] == 0x20 && v6.s6_addr[1] == 0x02) return false; // 6to4
  if (v6.s6_addr[0] == 0x20 && v6.s6_addr[1] == 0x01 &&
      ((v6.s6_addr[2] == 0 && v6.s6_addr[3] == 0) ||
       (v6.s6_addr[2] == 0x0d && v6.s6_addr[3] == 0xb8))) return false;
  return true;
}
}
