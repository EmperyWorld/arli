#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>
namespace arli::redact::store {
struct Fingerprint {size_t size;std::uint64_t rolling;std::array<unsigned char,32> digest;};
std::uint64_t rolling(std::string_view value);
Fingerprint fingerprint(std::string_view value);
void enable(const std::filesystem::path& path);
void append(std::string_view value);
std::vector<Fingerprint> records();
}
