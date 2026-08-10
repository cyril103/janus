#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <algorithm>
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
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
    expect(false, "invalid numeric conversion must fail");
  } catch (const janus::CompileError &error) {
    if (std::string_view{error.what()}.find(expected_message) ==
        std::string_view::npos) {
      std::cerr << "FAILED: expected '" << expected_message << "', got '"
                << error.what() << "'\n";
      ++failures;
    }
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
enum NumericCastError derives Copy {
    Overflow,
    Underflow,
    IncompatibleSign,
    NonFinite,
    FractionalLoss,
    PrecisionLoss
}
enum Result[T, E] { Ok(T), Error(E) }

val foldedSigned : byte = saturatingCast[byte](300)
val foldedUnsigned : ubyte = truncatingCast[ubyte](-1)
val directFloat : float = 0.1f

def main() : int {
    val signedValue : long = long(-129)
    val unsignedValue : ulong = ulong(300)
    val fraction : double = -12.75
    val single : float = 16777216.0f

    val a : byte = saturatingCast[byte](signedValue)
    val b : ubyte = saturatingCast[ubyte](unsignedValue)
    val c : short = truncatingCast[short](fraction)
    val d : ubyte = truncatingCast[ubyte](signedValue)
    val e : double = match checkedCast[double](single) {
        Ok(value) => value,
        Error(error) => 0.0
    }
    val f : Result[float, NumericCastError] = checkedCast[float](16777217)
    return int(a) + int(b) + int(c) + int(d) + int(e)
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "numeric_conversion");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("constant i8 127") != std::string::npos,
         "saturating casts participate in constant folding");
  expect(ir.find("constant i8 -1") != std::string::npos,
         "truncating casts participate in constant folding");
  expect(ir.find("float 0x3FB99999A0000000") != std::string::npos,
         "f-suffixed literal is represented directly as float");

  expect_compile_error(
      "def main() : int { val x : int = saturatingCast[int](true) return x }",
      "saturatingCast requires numeric source and destination types");
  expect_compile_error(
      "def main() : int { val x : int = truncatingCast[int](\"1\") return x }",
      "truncatingCast requires numeric source and destination types");
  expect_compile_error(
      "def main() : int { val x : int = checkedCast[int](1, 2) return x }",
      "checkedCast expects one destination type and one value argument");
  expect_compile_error(
      "def convert[T](value : int) : T { return saturatingCast[T](value) } "
      "def main() : int { return 0 }",
      "saturatingCast requires a concrete numeric destination type");
  expect_compile_error(
      "def main() : int { val x : float = 1.0ff return 0 }",
      "invalid float literal");

  janus::frontend::Parser diagnostic_parser{R"(
def main() : int {
    val source : long = long(300)
    val narrowed : byte = byte(source)
    return int(narrowed)
}
)"};
  const janus::semantic::AnalysisResult diagnostics =
      analyzer.analyze(diagnostic_parser.parse_program());
  expect(std::any_of(
             diagnostics.diagnostics.begin(), diagnostics.diagnostics.end(),
             [](const janus::Diagnostic &diagnostic) {
               return diagnostic.code ==
                          janus::DiagnosticCode::AnalyzerLossyNumericCast &&
                      !diagnostic.notes.empty() &&
                      diagnostic.notes.front().find("checkedCast[T]") !=
                          std::string::npos &&
                      diagnostic.notes.front().find("saturatingCast[T]") !=
                          std::string::npos &&
                      diagnostic.notes.front().find("truncatingCast[T]") !=
                          std::string::npos;
             }),
         "lossy-cast diagnostic recommends every explicit policy");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "numeric conversion policies are typed and backend-stable\n";
  return 0;
}
