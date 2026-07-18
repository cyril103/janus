#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

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

void expect_compile_error(std::string_view source,
                          std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    expect(false, "invalid generic method declaration must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "generic method error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Converter[T]() {
    def identity[U](value : U) : U {
        return value
    }
}

def main() : int {
    return 0
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.classes.front().methods.front().type_parameters.size() == 1 &&
             program.classes.front().methods.front().type_parameters.front() ==
                 "U",
         "parser retains generic method type parameters");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  expect_compile_error(
      "class Box[T]() { def invalid[T](value : T) : T { return value } } "
      "def main() : int { return 0 }",
      "type parameter 'T' is already declared");
  expect_compile_error(
      "class Box() { def invalid[int](value : int) : int { return value } } "
      "def main() : int { return 0 }",
      "conflicts with a built-in type");
  expect_compile_error(
      "class Box() { def invalid[T](value : Unknown) : T { return value } } "
      "def main() : int { return 0 }",
      "unknown type 'Unknown'");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "generic method declarations are type-checked\n";
  return 0;
}
