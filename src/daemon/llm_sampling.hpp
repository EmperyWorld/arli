#pragma once
#include <cmath>

namespace arli {
// -1 keeps the selected profile's historical default; zero is an explicit value.
inline bool valid_llm_temperature(double value) {
  return std::isfinite(value) && (value == -1.0 || (value >= 0.0 && value <= 2.0));
}
}
