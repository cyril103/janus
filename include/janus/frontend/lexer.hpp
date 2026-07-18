#pragma once

#include "janus/frontend/token.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace janus::frontend {

class Lexer final {
public:
  explicit Lexer(std::string_view source) noexcept;

  [[nodiscard]] Token next();

private:
  [[nodiscard]] bool at_end() const noexcept;
  [[nodiscard]] char current() const noexcept;
  [[nodiscard]] SourceLocation location() const noexcept;
  void advance() noexcept;
  void skip_whitespace() noexcept;

  std::string_view source_;
  std::size_t position_{};
  std::uint32_t line_{1};
  std::uint32_t column_{1};
};

} // namespace janus::frontend
