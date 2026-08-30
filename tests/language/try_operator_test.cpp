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
trait Try { type Output type Residual }
enum Option[T] { Some(T), None }
enum Result[T, E] { Ok(T), Error(E) }
enum Attempt[T, E] extends Try {
    type Output = T
    type Residual = E
    Continue(T), Stop(E)
}
enum ConcreteTry extends Try {
    type Output = int
    type Residual = int
    Passed(int), Failed(int)
}
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
def attemptValue(input : Attempt[int, string]) : Attempt[double, string] {
    val value : int = input?
    return Attempt.Continue[double, string](double(value))
}
def concreteValue(input : ConcreteTry) : ConcreteTry {
    val value : int = input?
    return ConcreteTry.Passed(value)
}
def propagate[P <: Try](value : P, scoped wrap : (P.Output) => P) : P {
    defer delete wrap
    val output : P.Output = value?
    return wrap(output)
}
def lambdaBoundary() : int {
    val transform : (Option[int]) => Option[int] = input => {
        val value : int = input?
        return Option.Some[int](value + 1)
    }
    val propagated : Option[int] = transform(Option.None[int]())
    delete transform
    return match propagated {
        Some(value) => value,
        None => 7
    }
}
def resultLambdaBoundary() : int {
    val transform : (Result[int, string]) => Result[double, string] = input => {
        val value : int = input?
        return Result.Ok[double, string](double(value) + 0.5)
    }
    val propagated : Result[double, string] =
        transform(Result.Error[int, string]("lambda failure"))
    delete transform
    return match propagated {
        Ok(value) => int(value),
        Error(message) => 9
    }
}
def invokeOption(
scoped transform : (Option[int]) => Option[int],
input : Option[int]
) : Option[int] {
    defer delete transform
    return transform(input)
}
def contextualCallbackBoundary() : int {
    val propagated : Option[int] = invokeOption(
        input => {
            val value : int = input?
            return Option.Some[int](value + 1)
        },
        Option.None[int]()
    )
    return match propagated {
        Some(value) => value,
        None => 0
    }
}
class OuterResource() {}
def lambdaKeepsOuterOwner() : int {
    val resource : OuterResource = new OuterResource()
    val transform : (Option[int]) => Option[int] = input => {
        val value : int = input?
        return Option.Some[int](value)
    }
    val propagated : Option[int] = transform(Option.None[int]())
    delete transform
    delete resource
    return match propagated {
        Some(value) => value,
        None => 0
    }
}
def main() : int {
    val option : Option[double] =
        optionValue(Option.Some[int](42))
    val optionValue : int = match option {
        Some(value) => int(value),
        None => 0
    }
    val generic : Attempt[int, int] = propagate[Attempt[int, int]](
        Attempt.Stop[int, int](5),
        value => Attempt.Continue[int, int](value)
    )
    val genericValue : int = match generic {
        Continue(value) => value,
        Stop(residual) => residual
    }
    return optionValue + lambdaBoundary() + resultLambdaBoundary() + genericValue - 21
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  std::size_t lambda_early_exit_warnings = 0;
  for (const janus::Diagnostic &diagnostic : analysis.diagnostics)
    if (diagnostic.code == janus::DiagnosticCode::AnalyzerUnprotectedEarlyExit)
      ++lambda_early_exit_warnings;
  expect(lambda_early_exit_warnings == 0,
         "lambda propagation does not treat outer owners as lambda locals");
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
  expect(ir.find("%enum.Attempt__double__string") != std::string::npos,
         "a user-defined Try implementation is propagated");
  expect(ir.find("define %enum.ConcreteTry @concreteValue") !=
             std::string::npos,
         "equal concrete Output and Residual types keep distinct branches");
  expect(ir.find("define internal %enum.Option__int @__janus_lambda_body_") !=
             std::string::npos,
         "Option propagation is lowered inside the lambda body function");
  expect(ir.find("define internal %enum.Result__double__string "
                 "@__janus_lambda_body_") != std::string::npos,
         "Result propagation is lowered inside the lambda body function");

  expect_compile_error(
      std::string{declarations} +
          "def bad(value : Option[int]) : int { return value? } "
          "def main() : int { return 0 }",
      "enclosing function to return a type implementing Try");
  expect_compile_error(
      std::string{declarations} +
          "def bad(value : Result[int, string]) : Result[int, int] { "
          "val item : int = value? return Result.Ok[int, int](item) } "
          "def main() : int { return 0 }",
      "cannot propagate residual type 'string'");
  expect_compile_error(
      std::string{declarations} +
          "def main() : int { val value : int = 1? return value }",
      "requires a type implementing Try");
  expect_compile_error(
      std::string{declarations} +
          "def main() : int { val transform = (input : Option[int]) => { "
          "val item : int = input? return Option.Some[int](item) } "
          "delete transform return 0 }",
      "requires a contextual function return type");
  expect_compile_error(
      std::string{declarations} +
          "def main() : int { val transform : (Result[int, string]) => "
          "Result[int, int] = input => { val item : int = input? "
          "return Result.Ok[int, int](item) } delete transform return 0 }",
      "cannot propagate residual type 'string' from a lambda returning residual type "
      "'int'");
  expect_compile_error(std::string{declarations} +
                           "def main() : int { val transform : (Option[int]) "
                           "=> int = input => { "
                           "return input? } delete transform return 0 }",
                       "requires the enclosing lambda to return a type implementing Try");
  expect_compile_error(
      std::string{declarations} +
          "def main() : int { val outer : () => Option[int] = () => { "
          "val inner : (Option[int]) => int = input => input? "
          "delete inner return Option.None[int]() } delete outer return 0 }",
      "inside a lambda requires a block body");

  const std::string leaking_lambda_source = std::string{declarations} + R"(
class Resource() {}
def main() : int {
    val transform : (Option[int]) => Option[int] = input => {
        val resource : Resource = new Resource()
        val value : int = input?
        delete resource
        return Option.Some[int](value)
    }
    delete transform
    return 0
}
)";
  janus::frontend::Parser leaking_lambda_parser{leaking_lambda_source};
  const janus::semantic::AnalysisResult leaking_lambda_analysis =
      analyzer.analyze(leaking_lambda_parser.parse_program());
  std::size_t local_early_exit_warnings = 0;
  for (const janus::Diagnostic &diagnostic :
       leaking_lambda_analysis.diagnostics)
    if (diagnostic.code == janus::DiagnosticCode::AnalyzerUnprotectedEarlyExit)
      ++local_early_exit_warnings;
  expect(local_early_exit_warnings == 1,
         "lambda propagation warns about an unprotected lambda-local owner");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "? propagates None and Error values\n";
  return 0;
}
