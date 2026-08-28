#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void expect_error(std::string_view source, std::string_view message,
                  std::size_t line = 1) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
    expect(false, "invalid expression body must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(message) != std::string_view::npos,
           "diagnostic explains the expression-body error");
    expect(error.location().line == line,
           "diagnostic points at the useful expression line");
  }
}

std::string diagnostic_for(std::string_view source) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
  } catch (const janus::CompileError &error) {
    return error.what();
  }
  return {};
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
struct Pair(val left : int, val right : int) {}
enum Choice { Number(int), Empty }
class Box(val value : int) {
    borrow def read() : int => value
    consume def take() : int => value
}
trait Readable {
    borrow def read() : int
}
class ReadableBox(val value : int) extends Readable {
    borrow def read() : int => value
}
class Visibility() {
    private def hidden() : int => 1
    internal def local() : int => 2
}
def square(value : int) : int => value * value
def pair(value : int) : Pair => new Pair(value, value + 1)
def choice(value : int) : Choice => Choice.Number(value)
def box(value : int) : Box => new Box(value)
def transfer(value : Box) : Box => move value
def pointer() : Ptr[int] => null[int]()
def identity[T](value : T) : T => value
def callback() : (int) => int => (value : int) => value + 1
def borrowed(borrow value : Box) : borrow Box => value
def discard() : Unit => println("discard")
def unitExpression() : Unit { return }
def unit() : Unit => unitExpression()
const def twice(value : int) : int => value * 2
tailrec def countdown(value : int) : int => countdown(value - 1)
def choose(value : Choice) : int => match value { Number(number) => number, Empty => 0 }
def main() : int => square(twice(3))
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.functions.front().body.size() == 1,
         "an expression body normalizes to one statement");
  const auto *returned = std::get_if<janus::ast::ReturnStatement>(
      &program.functions.front().body.front());
  expect(returned != nullptr && returned->expression.has_value(),
         "an expression body normalizes to a value return");
  expect(returned != nullptr && returned->location.line == 18,
         "the synthetic return keeps the expression source location");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const auto module = generator.generate(program, "expression_body");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("ret i32") != std::string::npos,
         "backend lowers an expression body through return IR");
  expect(ir.find("countdown") != std::string::npos,
         "tailrec expression bodies reach backend lowering");

  janus::frontend::Parser const_parser{R"(
const def answer() : int => 6 * 7
const value : int = answer()
staticAssert(value == 42)
def main() : int => value
)"};
  static_cast<void>(analyzer.analyze(const_parser.parse_program()));

  expect_error("def wrong() => 1\ndef main() : int => 0", "expected ':'", 1);
  expect_error("def wrong() : int => true\ndef main() : int => 0", "int", 1);
  expect_error("def wrong() : Unit => 1\ndef main() : int => 0", "Unit", 1);
  expect_error("class Box() {} def wrong(borrow box : Box) : borrow Box => new Box() def main() : int => 0",
               "borrow", 1);
  expect_error("def wrong() : int => val value : int = 1\ndef main() : int => 0",
               "expression", 1);

  // Ordinary blocks deliberately retain the existing explicit-return rule.
  expect_error("def missing() : int { 42 } def main() : int => 0", "return", 1);

  const std::string expression_borrow_diagnostic = diagnostic_for(
      "class Box() {} def wrong(borrow value : Box) : Box => value "
      "def main() : int => 0");
  const std::string block_borrow_diagnostic = diagnostic_for(
      "class Box() {} def wrong(borrow value : Box) : Box { return value } "
      "def main() : int { return 0 }");
  expect(!expression_borrow_diagnostic.empty() &&
             expression_borrow_diagnostic == block_borrow_diagnostic,
         "expression and block bodies preserve ownership diagnostics");
  const std::string expression_move_diagnostic = diagnostic_for(
      "class Box() {} def wrong(borrow value : Box) : Box => move value "
      "def main() : int => 0");
  const std::string block_move_diagnostic = diagnostic_for(
      "class Box() {} def wrong(borrow value : Box) : Box { return move value } "
      "def main() : int { return 0 }");
  expect(!expression_move_diagnostic.empty() &&
             expression_move_diagnostic == block_move_diagnostic,
         "expression and block bodies preserve move diagnostics");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "expression bodies reuse explicit return semantics\n";
  return 0;
}
