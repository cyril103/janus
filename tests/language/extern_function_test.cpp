#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <memory>
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
    expect(false, "invalid external declaration must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "external declaration error contains the expected explanation");
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

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.functions.contains("c_add"),
         "an ABI-compatible external signature is analyzed");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "extern_function");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("declare i32 @c_add(i32, i32)") != std::string::npos,
         "extern def emits an LLVM declaration with the C symbol name");
  expect(ir.find("define i32 @c_add") == std::string::npos,
         "an external function has no LLVM body");
  expect(ir.find("call i32 @c_add(i32 20, i32 22)") != std::string::npos,
         "Janus calls the emitted external declaration");

  janus::frontend::Parser abi_parser{
      "extern def exchange(data : Ptr[int], size : usize, ratio : double, "
      "tag : byte, codepoint : char, enabled : bool) : Unit "
      "def main() : int { return 0 }"};
  static_cast<void>(analyzer.analyze(abi_parser.parse_program()));

  expect_compile_error(
      "extern def identity[T](value : T) : T "
      "def main() : int { return 0 }",
      "cannot be generic");
  expect_compile_error(
      "extern def print_string(value : string) : Unit "
      "def main() : int { return 0 }",
      "not compatible with the C ABI");
  expect_compile_error(
      "class Resource() {} extern def accept_resource(value : Resource) : Unit "
      "def main() : int { return 0 }",
      "not compatible with the C ABI");
  expect_compile_error("extern def main() : int",
                       "entry point 'main' cannot be external");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "external function syntax is parsed\n";
  return 0;
}
