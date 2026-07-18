#include "janus/ast/ast.hpp"
#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"
#include "janus/types/type.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <string>
#include <string_view>
#include <variant>

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
    expect(false, "invalid source must produce a compile error");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "compile error contains the expected explanation");
  }
}

} // namespace

int main() {
  janus::frontend::Parser parser{
      "def main() : int { val x : int = 5 return 0 }"};
  const janus::ast::Program program = parser.parse_program();

  expect(program.functions.size() == 1, "one function is parsed");
  expect(program.functions.front().name == "main",
         "the entry point is named main");
  expect(program.functions.front().return_type == &janus::Type::int_type(),
         "main returns int");
  expect(program.functions.front().body.size() == 2,
         "main contains a declaration and a return");
  if (program.functions.front().body.size() == 2) {
    const janus::ast::ValueDeclaration &declaration =
        std::get<janus::ast::ValueDeclaration>(
            program.functions.front().body.front());
    expect(declaration.name == "x", "the identifier is x");
    expect(declaration.declared_type == &janus::Type::int_type(),
           "the declared type is int");
    expect(!declaration.is_mutable, "a val declaration is immutable");
    expect(
        std::get<janus::ast::IntegerLiteralExpression>(declaration.initializer)
                .value == 5,
        "the initializer is the integer literal 5");
  }

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  const janus::semantic::SymbolTable &symbols = analysis.functions.at("main");
  expect(symbols.contains("x"), "x is entered in the symbol table");
  expect(!symbols.at("x").is_mutable, "x is immutable");
  expect(symbols.at("x").type->is_signed(), "x has a signed type");
  expect(symbols.at("x").type->bit_width() == 32, "x has a 32-bit type");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module = generator.generate(program);
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("%x = alloca i32") != std::string::npos,
         "LLVM allocates i32 storage for x");
  expect(ir.find("store i32 5, ptr %x") != std::string::npos,
         "LLVM stores the evaluated value 5 in x");
  expect(ir.find("ret i32 0") != std::string::npos,
         "LLVM returns the value from main");

  expect_compile_error("def main() : int { val x int = 5 return 0 }",
                       "expected ':'");
  expect_compile_error("def main() : int { val x : unknown = 5 return 0 }",
                       "unknown type 'unknown'");
  expect_compile_error("def main() : int { val x : int = 2147483648 return 0 }",
                       "outside the signed 32-bit range");
  expect_compile_error(
      "def main() : int { val x : int = 1; val x : int = 2 return 0 }",
      "value 'x' is already declared");
  expect_compile_error("def main() : int { val x : int = 5; x = 6 return 0 }",
                       "expected 'val' or 'return'");
  expect_compile_error("def helper() : int { return 0 }",
                       "must declare an entry point 'main'");
  expect_compile_error(
      "def main() : int { return 0 } def main() : int { return 0 }",
      "function 'main' is already declared");
  expect_compile_error("def main() : int { val x : int = 5 }",
                       "must return a value");
  expect_compile_error("def main() : int { return 0 val x : int = 5 }",
                       "statement after return is unreachable");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "val x : int = 5 -> typed declaration and LLVM store\n";
  return 0;
}
