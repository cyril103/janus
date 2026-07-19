#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>

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
    expect(false, "invalid move must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "move error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Box(val value : int) {
    consume def take() : int {
        val result : int = value
        delete this
        return result
    }
}

def main() : int {
    val box : Box = new Box(42)
    return box.take()
}
)";
  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));
  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  static_cast<void>(generator.generate(program, "move_semantics"));

  expect_compile_error(
      "class Box() {} def main() : int { val first : Box = new Box() "
      "val second : Box = move first delete second return first.value }",
      "used before initialization");
  expect_compile_error("def main() : int { val value : int = 1 "
                       "val other : int = move value return other }",
                       "move requires an owning");
  expect_compile_error(
      "class Box() {} def main() : int { val first : Box = new Box() "
      "val second : Box = move new Box() delete first delete second return 0 }",
      "move requires a local value identifier");
  expect_compile_error(
      "class Box() { consume def take() : int { delete this return 1 } } "
      "def main() : int { val box : Box = new Box() val value : int = "
      "box.take() return box.take() }",
      "used before initialization");
  expect_compile_error(
      "class Box() { consume def take() : int { delete this return 1 } } "
      "class Holder(val child : Box) { def takeChild() : int { "
      "return child.take() } } def main() : int { return 0 }",
      "consuming field 'child' requires an explicit move");
  expect_compile_error(
      "class Box() { consume def take() : int { delete this return 1 } } "
      "def main() : int { val box : Box = new Box() while true { "
      "box.take() } return 0 }",
      "cannot be consumed from a loop");
  expect_compile_error(
      "class Box() { consume def take() : int { delete this return 1 } } "
      "def main() : int { val box : Box = new Box() "
      "val action : () => int = () => box.take() delete action "
      "delete box return 0 }",
      "cannot be consumed from a loop, branch expression, or closure");
  expect_compile_error(
      "class Box() { consume def take() : int { delete this return 1 } } "
      "def main() : int { val box : Box = new Box() if true { box.take() } "
      "return box.take() }",
      "used before initialization");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "move transfers owning values and invalidates the source\n";
  return 0;
}
