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

  constexpr std::string_view aggregate_source = R"(
class Resource() {}
struct Box(val resource : Resource) {}
enum Holder { Some(Box), None }
def consumeBox(box : Box) : Unit {
    delete box
}
def transferBox(box : Box) : Box {
    return move box
}
def main() : int {
    val first : Box = new Box(new Resource())
    val second : Box = move first
    val holder : Holder = Holder.Some(move second)
    val movedHolder : Holder = move holder
    delete movedHolder
    val third : Box = new Box(new Resource())
    consumeBox(move third)
    val fourth : Box = new Box(new Resource())
    val fifth : Box = transferBox(move fourth)
    delete fifth
    return 0
}
)";
  janus::frontend::Parser aggregate_parser{aggregate_source};
  const janus::ast::Program aggregate_program =
      aggregate_parser.parse_program();
  static_cast<void>(analyzer.analyze(aggregate_program));
  llvm::LLVMContext aggregate_context;
  janus::backend::llvm::IrGenerator aggregate_generator{aggregate_context};
  static_cast<void>(
      aggregate_generator.generate(aggregate_program, "aggregate_moves"));

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
      "class Resource() {} struct Box(val resource : Resource) {} "
      "def main() : int { val first : Box = new Box(new Resource()) "
      "val second : Box = first delete first delete second return 0 }",
      "requires an explicit move");
  expect_compile_error(
      "class Resource() {} struct Box(val resource : Resource) {} "
      "enum Holder { Some(Box), None } def main() : int { "
      "val box : Box = new Box(new Resource()) "
      "val first : Holder = Holder.Some(move box) "
      "val second : Holder = move first delete second delete first return 0 }",
      "used before initialization");
  expect_compile_error(
      "class Resource() {} struct Box(val resource : Resource) {} "
      "def pass(box : Box) : Unit { delete box } def main() : int { "
      "val box : Box = new Box(new Resource()) pass(box) return 0 }",
      "requires an explicit move");
  expect_compile_error(
      "class Resource() {} struct Box(val resource : Resource) {} "
      "def pass(box : Box) : Box { return box } "
      "def main() : int { return 0 }",
      "requires an explicit move");
  expect_compile_error(
      "class Resource() {} struct Box(val resource : Resource) {} "
      "enum Holder { Some(Box), None } def main() : int { "
      "val box : Box = new Box(new Resource()) "
      "val holder : Holder = Holder.Some(move box) "
      "val value : int = match holder { Some(item) => 1, None => 0 } "
      "delete holder return value }",
      "matching an owning enum requires an explicit move");
  expect_compile_error(
      "class Resource() {} struct Box(val resource : Resource) {} "
      "struct Outer(val box : Box) {} def pass(box : Box) : Unit { "
      "delete box } def main() : int { "
      "val outer : Outer = new Outer(new Box(new Resource())) "
      "pass(outer.box) delete outer return 0 }",
      "cannot be transferred independently");
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
