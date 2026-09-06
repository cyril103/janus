#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace janus::frontend::unicode {

struct DecodedScalar {
  char32_t value;
  std::size_t length;
};

[[nodiscard]] DecodedScalar decode(std::string_view source, std::size_t offset);
[[nodiscard]] bool is_xid_start(char32_t value) noexcept;
[[nodiscard]] bool is_xid_continue(char32_t value) noexcept;
[[nodiscard]] bool is_disallowed_identifier_control(char32_t value) noexcept;
[[nodiscard]] std::string normalize_nfc(std::string_view source);
[[nodiscard]] std::string_view unicode_version() noexcept;

} // namespace janus::frontend::unicode
