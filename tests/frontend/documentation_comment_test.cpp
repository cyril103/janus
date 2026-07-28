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
/// Utilities for [[Widget]] values.
module sample

/// A visible global.
val answer : int = 42

/// A printable value.
trait Printable {
    /// Renders this value.
    def render() : string
}

/// Result state.
enum Status {
    /// Work completed.
    Ready,
    /// Work failed.
    Failed
}

/// A documented value.
struct Widget() {
    /// Display name.
    val name : string = "widget"
    /// Returns the display name.
    def label() : string { return name }
}

/// Creates a [[Widget]].
def makeWidget() : Widget { return Widget() }
)"};
  const janus::ast::Program program = parser.parse_program();

  expect(program.documentation == "Utilities for [[Widget]] values.",
         "module documentation is retained");
  expect(program.globals[0].declaration.documentation == "A visible global.",
         "global documentation is retained");
  expect(program.traits[0].documentation == "A printable value.",
         "trait documentation is retained");
  expect(program.traits[0].methods[0].documentation == "Renders this value.",
         "trait method documentation is retained");
  expect(program.enums[0].documentation == "Result state.",
         "enum documentation is retained");
  expect(program.enums[0].cases[0].documentation == "Work completed.",
         "enum variant documentation is retained");
  expect(program.classes[0].documentation == "A documented value.",
         "type documentation is retained");
  expect(program.classes[0].fields[0].documentation == "Display name.",
         "field documentation is retained");
  expect(program.classes[0].methods[0].documentation ==
             "Returns the display name.",
         "method documentation is retained");
  expect(program.functions[0].documentation == "Creates a [[Widget]].",
         "function documentation is retained");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "documentation comments are represented in the AST\n";
  return 0;
}
