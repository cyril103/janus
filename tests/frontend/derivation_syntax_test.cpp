#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"

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

void expect_parse_error(std::string_view source,
                        std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    static_cast<void>(parser.parse_program());
    expect(false, "invalid derivation clause must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "derivation error contains the expected explanation");
  }
}

} // namespace

int main() {
  janus::frontend::Parser parser{R"(
struct Point(val x : int, val y : int)
derives Copy, Equality, Hashing, Debug {}

enum Status[T] derives Equality, Debug {
    Ready(T),
    Failed
}

trait Printable {}

class Report(val message : string)
extends Printable derives Equality, Hashing, Debug {}

def main() : int { return 0 }
)"};
  const janus::ast::Program program = parser.parse_program();

  expect(program.classes.size() == 2,
         "struct and class derivations are retained");
  expect(program.enums.size() == 1, "enum derivations are retained");
  if (program.classes.size() == 2) {
    expect(program.classes[0].derivations.size() == 4,
           "all struct derivations are represented");
    expect(program.classes[0].derivations[0].kind ==
               janus::ast::DerivationKind::Copy,
           "Copy retains its typed derivation kind");
    expect(program.classes[0].derivations[3].kind ==
               janus::ast::DerivationKind::Debug,
           "Debug retains its typed derivation kind");
    expect(program.classes[1].derivations.size() == 3,
           "class derivations are represented");
  }
  if (program.enums.size() == 1) {
    expect(program.enums[0].derivations.size() == 2,
           "enum derivations are represented");
    expect(program.enums[0].derivations[0].kind ==
               janus::ast::DerivationKind::Equality,
           "Equality retains its typed derivation kind");
  }

  expect_parse_error(
      "struct Invalid() derives Copy, Copy {} "
      "def main() : int { return 0 }",
      "derivation 'Copy' is requested more than once");
  expect_parse_error(
      "enum Invalid derives Display { Value } "
      "def main() : int { return 0 }",
      "unknown derivation 'Display'");
  expect_parse_error(
      "class Invalid() derives {} def main() : int { return 0 }",
      "expected a derivation name after 'derives'");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "derivation requests are explicit typed syntax\n";
  return 0;
}
