#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void expect_top_level_declaration_error(std::string_view source,
                                        std::uint32_t expected_line,
                                        std::uint32_t expected_column,
                                        std::string_view keyword) {
  try {
    janus::frontend::Parser parser{source};
    static_cast<void>(parser.parse_program());
    expect(false, "top-level declaration must fail during parsing");
  } catch (const janus::CompileError &error) {
    const std::string_view message{error.what()};
    expect(message.find("top-level val/var declarations are not supported") !=
               std::string_view::npos,
           "error explains that top-level val/var is unsupported");
    expect(message.find("move the declaration into a function") !=
               std::string_view::npos,
           "error recommends moving the declaration into a function");
    expect(message.find("expose it through a function") !=
               std::string_view::npos,
           "error recommends exposing module data through a function");
    expect(error.location().line == expected_line,
           "error points at the declaration line");
    expect(error.location().column == expected_column,
           "error points at the declaration column");
    expect(message.find(keyword) != std::string_view::npos,
           "error includes the offending keyword");
  }
}

} // namespace

int main() {
  expect_top_level_declaration_error("val answer : int = 42\n", 1, 1, "val");
  expect_top_level_declaration_error(
      "module sample\nimport std.array\nvar answer : int = 42\n", 3, 1,
      "var");

  try {
    janus::frontend::Parser parser{"return 0\n"};
    static_cast<void>(parser.parse_program());
    expect(false, "other invalid top-level tokens must still fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find("expected 'def', found "
                                               "'return'") !=
               std::string_view::npos,
           "non val/var top-level diagnostics keep the generic parser error");
    expect(error.location().line == 1, "generic error line is unchanged");
    expect(error.location().column == 1, "generic error column is unchanged");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "top-level val/var declarations produce actionable parser "
               "diagnostics\n";
  return 0;
}
