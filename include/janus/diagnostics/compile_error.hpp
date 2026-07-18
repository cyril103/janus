#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace janus {

struct SourceLocation {
  std::size_t offset{};
  std::uint32_t line{1};
  std::uint32_t column{1};
};

class CompileError final : public std::runtime_error {
public:
  CompileError(SourceLocation location, std::string message)
      : std::runtime_error{std::move(message)}, location_{location} {}

  [[nodiscard]] SourceLocation location() const noexcept { return location_; }

private:
  SourceLocation location_;
};

} // namespace janus
