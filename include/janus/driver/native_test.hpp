#pragma once

#include "janus/diagnostics/compile_error.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace janus::driver {

struct NativeTest {
  std::filesystem::path source;
  std::string name;
  std::string identifier;
  SourceLocation location;
  bool ignored{};
  bool serial{};
  std::optional<std::string> expected_panic;
  bool legacy{};
};

[[nodiscard]] std::vector<NativeTest>
discover_native_tests(const std::filesystem::path &tests_root);

[[nodiscard]] bool matches_native_test_filter(const NativeTest &test,
                                              std::string_view filter,
                                              bool exact);

[[nodiscard]] std::string native_test_source(std::string_view source,
                                             const NativeTest &test);

} // namespace janus::driver
