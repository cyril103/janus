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

} // namespace

int main() {
  janus::frontend::Parser parser{
      "extern def c_add(left : int, right : int) : int "
      "def main() : int { return c_add(20, 22) }"};
  const janus::ast::Program program = parser.parse_program();

  expect(program.functions.size() == 2,
         "extern and Janus functions are top-level declarations");
  expect(program.functions[0].name == "c_add",
         "the external function name is preserved");
  expect(program.functions[0].is_external,
         "extern def is represented explicitly in the AST");
  expect(program.functions[0].body.empty(),
         "an external function declaration has no Janus body");
  expect(program.functions[0].parameters.size() == 2,
         "external function parameters are parsed");
  expect(!program.functions[1].is_external,
         "ordinary function declarations remain non-external");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "external function syntax is parsed\n";
  return 0;
}
