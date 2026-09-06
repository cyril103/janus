#pragma once

#include <optional>
#include <string>

namespace janus::semantic {

[[nodiscard]] inline bool private_declaration_is_visible(
    bool is_private, const std::optional<std::string> &declaring_module,
    const std::optional<std::string> &context_module) noexcept {
  return !is_private || declaring_module == context_module;
}

} // namespace janus::semantic
