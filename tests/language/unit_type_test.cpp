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

void expect_compile_error(std::string_view source,
                          std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    expect(false, "invalid Unit source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "Unit error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Counter(var value : int) {
    def set(next : int) : Unit {
        value = next
    }
}
def observe(value : int) : Unit {
    return
}
def main() : int {
    val counter : Counter = new Counter(0)
    observe(42)
    counter.set(42)
    val result : int = counter.value
    delete counter
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(std::holds_alternative<janus::ast::ExpressionStatement>(
             program.functions.back().body[1]),
         "a function call can be an expression statement");
  const auto &bare_return =
      std::get<janus::ast::ReturnStatement>(program.functions.front().body[0]);
  expect(!bare_return.expression.has_value(), "Unit supports a bare return");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "unit_type");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("define void @observe(i32 %value)") != std::string::npos,
         "Unit lowers to an LLVM void return type");
  expect(ir.find("call void @observe(i32 42)") != std::string::npos,
         "a Unit function can be called as a statement");
  expect(ir.find("call void @Counter__set") != std::string::npos,
         "a Unit method can be called as a statement");
  expect(ir.find("ret void") != std::string::npos,
         "Unit functions return void");

  expect_compile_error(
      "def action() : Unit { return 1 } def main() : int { return 0 }",
      "cannot return a value");
  expect_compile_error("def main() : int { return }",
                       "return requires a value");
  expect_compile_error(
      "def action() : Unit {} def main() : int { val x : Unit = action() "
      "return 0 }",
      "Unit cannot be used as a value type");
  expect_compile_error(
      "def action(value : Unit) : Unit {} def main() : int { return 0 }",
      "Unit cannot be used as a parameter type");
  expect_compile_error(
      "def main() : int { val value : int = 1 value return value }",
      "only function and method calls");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "Unit and expression statements lower to LLVM void calls\n";
  return 0;
}
