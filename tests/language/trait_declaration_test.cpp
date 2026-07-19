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
    expect(false, "invalid trait declaration must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "trait error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Iterator[T]() {}

trait Iterable[T] {
    def iterator() : Iterator[T]
    def transform[U](value : T, function : (T) => U) : U
}

def main() : int {
    return 0
}
)";
  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.traits.size() == 1, "one trait is parsed");
  expect(program.traits.front().name == "Iterable",
         "the trait retains its name");
  expect(program.traits.front().type_parameters.size() == 1,
         "generic trait parameters are parsed");
  expect(program.traits.front().methods.size() == 2,
         "trait method signatures are parsed without bodies");
  expect(program.traits.front().methods[1].type_parameters.size() == 1,
         "trait methods can be generic");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  expect_compile_error("trait Duplicate[T, T] {} def main() : int { return 0 }",
                       "type parameter 'T' is already declared");
  expect_compile_error(
      "trait Duplicate { def value() : int def value() : int } "
      "def main() : int { return 0 }",
      "trait method 'value' is already declared");
  expect_compile_error("trait Invalid { def value() : Missing } "
                       "def main() : int { return 0 }",
                       "unknown type 'Missing'");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "generic trait signatures are parsed and validated\n";
  return 0;
}
