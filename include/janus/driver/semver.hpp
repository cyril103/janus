#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace janus::driver {

struct SemanticVersion {
  std::uint64_t major{};
  std::uint64_t minor{};
  std::uint64_t patch{};
  std::vector<std::string> prerelease;

  [[nodiscard]] std::string str() const;
};

[[nodiscard]] SemanticVersion parse_semantic_version(std::string_view text);
[[nodiscard]] std::strong_ordering compare(const SemanticVersion &left,
                                           const SemanticVersion &right);
[[nodiscard]] bool matches_version(std::string_view requirement,
                                   const SemanticVersion &version);

} // namespace janus::driver
