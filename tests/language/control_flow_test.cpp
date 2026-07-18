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
    expect(false, "invalid control-flow source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "control-flow error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def main() : int {
    var index : int = 1
    var total : int = 0
    while index <= 5 {
        total = total + index
        index = index + 1
    }
    if total == 15 {
        return 42
    } else {
        return 0
    }
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(std::holds_alternative<std::shared_ptr<janus::ast::WhileStatement>>(
             program.functions[0].body[2]),
         "while is represented in the AST");
  expect(std::holds_alternative<std::shared_ptr<janus::ast::IfStatement>>(
             program.functions[0].body[3]),
         "if/else is represented in the AST");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "control_flow");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("while.condition") != std::string::npos,
         "while creates a condition block");
  expect(ir.find("if.then") != std::string::npos &&
             ir.find("if.else") != std::string::npos,
         "if/else creates distinct LLVM blocks");
  expect(ir.find("br i1") != std::string::npos,
         "conditions generate conditional branches");

  constexpr std::string_view definitely_initialized = R"(
def main() : int {
    var result : int
    if true {
        result = 1
    } else {
        result = 2
    }
    return result
}
)";
  janus::frontend::Parser initialized_parser{definitely_initialized};
  const janus::ast::Program initialized_program =
      initialized_parser.parse_program();
  static_cast<void>(analyzer.analyze(initialized_program));

  expect_compile_error(
      "def main() : int { var x : int if true { x = 1 } return x }",
      "used before initialization");
  expect_compile_error(
      "def main() : int { var x : int while false { x = 1 } return x }",
      "used before initialization");
  expect_compile_error(
      "def main() : int { if 1 { return 1 } else { return 0 } }",
      "where type 'bool' is required");
  expect_compile_error(
      "def main() : int { if true { return 1 } else { return 0 } return 2 }",
      "statement after return is unreachable");
  expect_compile_error("def main() : int { while true { return 1 } }",
                       "must return a value");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "if/else and while generate typed control flow\n";
  return 0;
}
