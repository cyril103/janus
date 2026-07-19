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

  janus::frontend::Parser alias_parser{
      "extern(\"abs\") def absolute(value : int) : int "
      "def main() : int { return absolute(-42) }"};
  const janus::ast::Program alias_program = alias_parser.parse_program();
  expect(alias_program.functions[0].external_symbol == "abs",
         "an explicit external symbol is preserved in the AST");
  static_cast<void>(analyzer.analyze(alias_program));
  llvm::LLVMContext alias_context;
  janus::backend::llvm::IrGenerator alias_generator{alias_context};
  const std::unique_ptr<llvm::Module> alias_module =
      alias_generator.generate(alias_program, "external_alias");
  std::string alias_ir;
  llvm::raw_string_ostream alias_output{alias_ir};
  alias_module->print(alias_output, nullptr);
  alias_output.flush();
  expect(alias_ir.find("declare i32 @abs(i32)") != std::string::npos &&
             alias_ir.find("call i32 @abs(i32 -42)") != std::string::npos,
         "external aliases select the native declaration and call symbol");
  expect(alias_ir.find("@absolute") == std::string::npos,
         "the Janus alias is not exported as a second native symbol");

  janus::frontend::Parser cstr_parser{
      "extern def puts(text : Ptr[byte]) : int "
      "def main() : int { val text : string = \"Janus\" "
      "return puts(cstr(text)) }"};
  const janus::ast::Program cstr_program = cstr_parser.parse_program();
  static_cast<void>(analyzer.analyze(cstr_program));
  llvm::LLVMContext cstr_context;
  janus::backend::llvm::IrGenerator cstr_generator{cstr_context};
  const std::unique_ptr<llvm::Module> cstr_module =
      cstr_generator.generate(cstr_program, "c_string");
  std::string cstr_ir;
  llvm::raw_string_ostream cstr_output{cstr_ir};
  cstr_module->print(cstr_output, nullptr);
  cstr_output.flush();
  expect(cstr_ir.find("cstr.data = extractvalue { ptr, i64 }") !=
             std::string::npos &&
             cstr_ir.find("call i32 @puts(ptr %cstr.data)") !=
                 std::string::npos,
         "cstr exposes the null-terminated UTF-8 data pointer to C");

  janus::frontend::Parser variadic_parser{
      "extern def printf(format : Ptr[byte], ...) : int "
      "def main() : int { return printf(cstr(\"%d %d %u %llu %.1f\"), "
      "byte(-7), true, char(65), usize(9), 2.5) }"};
  const janus::ast::Program variadic_program =
      variadic_parser.parse_program();
  expect(variadic_program.functions[0].is_variadic,
         "the ellipsis is represented explicitly in the AST");
  static_cast<void>(analyzer.analyze(variadic_program));
  llvm::LLVMContext variadic_context;
  janus::backend::llvm::IrGenerator variadic_generator{variadic_context};
  const std::unique_ptr<llvm::Module> variadic_module =
      variadic_generator.generate(variadic_program, "variadic_function");
  std::string variadic_ir;
  llvm::raw_string_ostream variadic_output{variadic_ir};
  variadic_module->print(variadic_output, nullptr);
  variadic_output.flush();
  expect(variadic_ir.find("declare i32 @printf(ptr, ...)") !=
             std::string::npos,
         "variadic extern def emits an LLVM variadic declaration");
  expect(variadic_ir.find("i32 -7, i32 1, i32 65, i64 9, double") !=
             std::string::npos,
         "byte and bool receive the C default integer promotions");
  expect(variadic_ir.find("call i32 (ptr, ...) @printf") != std::string::npos,
         "calls retain the variadic LLVM function type");

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
  expect_compile_error(
      "extern(\"\") def empty() : Unit def main() : int { return 0 }",
      "external symbol name cannot be empty");
  expect_compile_error(
      "extern(\"same\") def first() : Unit "
      "extern(\"same\") def second() : Unit "
      "def main() : int { return 0 }",
      "already bound");
  expect_compile_error(
      "def native() : Unit {} extern(\"native\") def alias() : Unit "
      "def main() : int { return 0 }",
      "conflicts with Janus function");
  expect_compile_error("def main() : int { cstr() return 0 }",
                       "cstr expects one string argument");
  expect_compile_error("def main() : int { cstr(42) return 0 }",
                       "where type 'string' is required");
  expect_compile_error(
      "def invalid(value : int, ...) : int { return value } "
      "def main() : int { return 0 }",
      "only external functions can be variadic");
  expect_compile_error(
      "extern def invalid(...) : int def main() : int { return 0 }",
      "requires a fixed parameter");
  expect_compile_error(
      "extern def printf(format : Ptr[byte], ...) : int "
      "def main() : int { return printf(cstr(\"%s\"), \"text\") }",
      "variadic C argument has incompatible type 'string'");
  expect_compile_error(
      "extern def printf(format : Ptr[byte], ...) : int "
      "def main() : int { return printf() }",
      "expects at least 1 argument");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "external function syntax is parsed\n";
  return 0;
}
