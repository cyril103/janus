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
    expect(false, "invalid if expression must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "if expression error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def main() : int {
    val absolute : (int) => int =
        (value : int) => if value < 0 { -value } else { value }
    val result : int = if true { absolute(-42) } else { 0 }
    delete absolute
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "if_expression");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("if.value.then") != std::string::npos,
         "if expressions emit a then block");
  expect(ir.find("phi i32") != std::string::npos,
         "if expressions merge their values with a phi");

  expect_compile_error("def main() : int { return if 1 { 1 } else { 2 } }",
                       "where type 'bool' is required");
  expect_compile_error("def main() : int { return if true { 1 } else { 2.0 } }",
                       "branches must have the same type");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "if expressions produce strongly typed values\n";
  return 0;
}
