#include "janus/ast/ast.hpp"
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

void expect_valid(std::string_view source, bool generate_ir = false) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    if (generate_ir) {
      llvm::LLVMContext context;
      janus::backend::llvm::IrGenerator generator{context};
      expect(generator.generate(program, "immutable_borrow") != nullptr,
             "valid shared borrow program generates LLVM IR");
    }
  } catch (const std::exception &error) {
    std::cerr << "FAILED: valid source was rejected: " << error.what() << '\n';
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
    expect(false, "invalid shared borrow program must be rejected");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           expected_message);
    if (error.diagnostic().code == janus::DiagnosticCode::AnalyzerLegacy) {
      std::cerr << "FAILED: shared borrow error lacks a precise code: "
                << expected_message << '\n';
      ++failures;
    }
  }
}

} // namespace

int main() {
  expect_valid(R"(
class Box(var value : int) {
  borrow def read() : int { return value }
  def write(next : int) : Unit { value = next }
}
struct Pair(val left : int, val right : int) {}
def observe(borrow box : Box) : int { return box.read() }
def sum(borrow pair : Pair) : int { return pair.left + pair.right }
def choose(borrow box : Box, flag : bool) : int {
  if flag { return observe(box) }
  return box.read()
}
def main() : int {
  val box : Box = new Box(7)
  var total : int = 0
  if true {
    borrow val first : Box = box
    borrow val second : Box = box
    total = first.read() + second.read()
  }
  val selected : int = choose(box, false)
  var visits : int = 0
  while visits < 1 {
    borrow val loopView : Box = box
    total = total + loopView.read()
    visits = visits + 1
  }
  val pair : Pair = new Pair(2, 3)
  borrow val pairView : Pair = pair
  val pairTotal : int = sum(pairView)
  delete box
  return total + selected + pairTotal
}
)",
               true);

  expect_valid(R"(
trait Viewable { borrow def view() : int }
class Item(val value : int) extends Viewable {
  borrow def view() : int { return value }
}
def main() : int {
  val item : Item = new Item(3)
  val result : int = item.view()
  delete item
  return result
}
)");

  expect_compile_error(R"(
class Box(val value : int) { borrow def read() : int { return value } }
def main() : int {
  val box : Box = new Box(1)
  borrow val view : Box = box
  val moved : Box = move box
  delete moved
  return view.read()
}
)",
                       "cannot be released while borrowed");

  expect_compile_error(R"(
class Box(val value : int) { borrow def read() : int { return value } }
def main() : int {
  val box : Box = new Box(1)
  borrow val view : Box = box
  delete box
  return view.read()
}
)",
                       "cannot be released while borrowed");

  expect_compile_error(R"(
class Box(var value : int) {}
def main() : int {
  val box : Box = new Box(1)
  borrow val view : Box = box
  view.value += 2
  return 0
}
)",
                       "cannot mutate through shared borrow");

  expect_compile_error(R"(
class Box(var value : int) {}
def main() : int {
  val box : Box = new Box(1)
  borrow val view : Box = box
  box.value += 2
  return view.value
}
)",
                       "cannot be mutated while borrowed");

  expect_compile_error(R"(
class Box(var value : int) { def write(next : int) : Unit { value = next } }
def main() : int {
  val box : Box = new Box(1)
  borrow val view : Box = box
  view.write(2)
  return 0
}
)",
                       "can only call a borrow method");

  expect_compile_error(R"(
class Box(val value : int) {}
def escape(borrow box : Box) : Box { return box }
def main() : int { return 0 }
)",
                       "cannot escape by return");

  expect_compile_error(R"(
class Box(val value : int) {}
def take(box : Box) : Unit { delete box }
def observe(borrow box : Box) : Unit { take(box) }
def main() : int { return 0 }
)",
                       "cannot be passed to an owning parameter");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "immutable borrows enforce shared, lexical observation\n";
  return 0;
}
