#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace janus::driver {

enum class DoctestExpectation {
  CompilePass,
  CompileFail,
};

struct Doctest {
  std::filesystem::path document;
  std::size_t line{};
  std::string name;
  std::string source;
  DoctestExpectation expectation{DoctestExpectation::CompilePass};
  std::string expected_diagnostic;

  [[nodiscard]] std::string display_name() const;
};

[[nodiscard]] std::vector<Doctest>
parse_doctests(const std::filesystem::path &document,
               std::string_view markdown);

[[nodiscard]] std::vector<Doctest>
discover_doctests(const std::filesystem::path &package_root,
                  const std::vector<std::filesystem::path> &documentation);

[[nodiscard]] bool matches_doctest_filter(const Doctest &test,
                                          std::string_view filter);

} // namespace janus::driver
