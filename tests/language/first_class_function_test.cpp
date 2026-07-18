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
    expect(false, "invalid first-class function program must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "function value error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def apply[T](function : (T) => T, value : T) : T {
    return function(value)
}

def makeAdder(amount : int) : (int) => int {
    return (value : int) => value + amount
}

def makeIdentity[T]() : (T) => T {
    return (value : T) => value
}

def main() : int {
    val increment : (int) => int = (value : int) => value + 1
    val first : int = apply[int](increment, 41)
    delete increment

    val addTen : (int) => int = makeAdder(10)
    val second : int = addTen(first)
    delete addTen

    val identity : (int) => int = makeIdentity[int]()
    val result : int = identity(second)
    delete identity
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  const auto &main_body = program.functions.back().body;
  const auto &increment =
      std::get<janus::ast::ValueDeclaration>(main_body.front());
  expect(increment.declared_type.name == "Function" &&
             increment.declared_type.type_arguments.size() == 2,
         "the parser retains function signatures");
  expect(std::holds_alternative<janus::ast::LambdaExpression>(
             increment.initializer->value),
         "the parser retains lambda literals");

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.functions.at("main").at("increment").type.name() ==
             "(int) => int",
         "function values retain their semantic signature");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "first_class_function");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("%lambda.env.") != std::string::npos,
         "captured values are stored in closure environments");
  expect(ir.find("define internal i32 @__janus_lambda_") != std::string::npos,
         "lambda bodies lower to internal LLVM functions");
  expect(ir.find("call i32 %") != std::string::npos,
         "function values are invoked through indirect calls");
  expect(ir.find("call void @free(ptr") != std::string::npos,
         "delete releases closure environments");
  expect(ir.find("define { ptr, ptr } @makeIdentity__int()") !=
             std::string::npos,
         "generic factories specialize function values");

  expect_compile_error("def main() : int { val f : (int) => int = "
                       "(value : double) => int(value) delete f return 0 }",
                       "cannot use expression of type '(double) => int'");
  expect_compile_error(
      "def main() : int { val f : (int) => int = "
      "(value : int) => value val result : int = f() delete f return result }",
      "expects 1 argument");
  expect_compile_error("def main() : int { val f : (int) => int = "
                       "(value : int) => value delete f return f(1) }",
                       "used before initialization");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "lambdas are captured, generic first-class values\n";
  return 0;
}
