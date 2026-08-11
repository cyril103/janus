#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace janus {

struct Target {
  std::string triple{"x86_64-unknown-linux-gnu"};
  std::uint32_t pointer_width{64};

  void validate() const {
    if (pointer_width != 32 && pointer_width != 64)
      throw std::invalid_argument{"target pointer width must be 32 or 64"};
    if (triple.empty())
      throw std::invalid_argument{"target triple must not be empty"};
  }

  bool operator==(const Target &) const = default;
};

} // namespace janus
