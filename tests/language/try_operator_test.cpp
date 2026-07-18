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
    expect(false, "invalid propagation must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "propagation error contains the expected explanation");
  }
}

constexpr std::string_view declarations = R"(
enum Option[T] { Some(T), None }
enum Result[T, E] { Ok(T), Error(E) }
)";

} // namespace

int main() {
  const std::string source = std::string{declarations} + R"(
def optionValue(input : Option[int]) : Option[double] {
    val value : int = input?
    return Option.Some[double](double(value))
}
def resultValue(input : Result[int, string]) : Result[double, string] {
    val value : int = input?
    return Result.Ok[double, string](double(value))
}
def main() : int {
    val option : Option[double] =
        optionValue(Option.Some[int](42))
    return match option {
        Some(value) => int(value),
        None => 0
    }
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));
  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "try_operator");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("try.success") != std::string::npos,
         "? emits a success continuation");
  expect(ir.find("try.failure") != std::string::npos,
         "? emits an early-return path");
  expect(ir.find("%enum.Option__double") != std::string::npos,
         "Option propagation can change the success type");
  expect(ir.find("%enum.Result__double__string") != std::string::npos,
         "Result propagation preserves the error type");

  expect_compile_error(
      std::string{declarations} +
          "def bad(value : Option[int]) : int { return value? } "
          "def main() : int { return 0 }",
      "enclosing function to return Option");
  expect_compile_error(
      std::string{declarations} +
          "def bad(value : Result[int, string]) : Result[int, int] { "
          "val item : int = value? return Result.Ok[int, int](item) } "
          "def main() : int { return 0 }",
      "cannot propagate error type 'string'");
  expect_compile_error(
      std::string{declarations} +
          "def main() : int { val value : int = 1? return value }",
      "requires an Option[T] or Result[T, E]");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "? propagates None and Error values\n";
  return 0;
}
