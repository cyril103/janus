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
  janus::frontend::Parser parser{R"(
module sample
import std.array
val answer : int = 42
var requests : usize = 0
var pending : int
private val secret : bool = true
def main() : int { return answer }
)"};
  const janus::ast::Program program = parser.parse_program();

  expect(program.globals.size() == 4, "four globals are parsed");
  if (program.globals.size() == 4) {
    const auto &answer = program.globals[0];
    expect(answer.declaration.name == "answer", "global val keeps its name");
    expect(!answer.declaration.is_mutable, "global val is immutable");
    expect(answer.declaration.initializer.has_value(),
           "global val keeps its initializer");
    expect(answer.module_name == "sample",
           "global val keeps its declaring module");

    const auto &requests = program.globals[1];
    expect(requests.declaration.is_mutable, "global var is mutable");
    expect(requests.declaration.initializer.has_value(),
           "initialized global var keeps its initializer");

    const auto &pending = program.globals[2];
    expect(!pending.declaration.initializer.has_value(),
           "parser represents an uninitialized global var");

    const auto &secret = program.globals[3];
    expect(secret.declaration.is_private, "private global keeps its visibility");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "top-level val/var declarations are represented in the AST\n";
  return 0;
}
