#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

std::size_t lossy_cast_warning_count(
    std::string_view source,
    janus::semantic::AnalysisOptions options = {}) {
  janus::frontend::Parser parser{source};
  const janus::semantic::AnalysisResult analysis =
      janus::semantic::Analyzer{}.analyze(parser.parse_program(), options);
  return static_cast<std::size_t>(std::count_if(
      analysis.diagnostics.begin(), analysis.diagnostics.end(),
      [](const janus::Diagnostic &diagnostic) {
        return diagnostic.code ==
               janus::DiagnosticCode::AnalyzerLossyNumericCast;
      }));
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
enum NumericCastError derives Copy {
    PrecisionLoss,
    FractionalLoss,
    NonFinite,
    IncompatibleSign,
    Underflow,
    Overflow
}
enum Result[T, E] { Error(E), Ok(T) }

val foldedSigned : byte = saturatingCast[byte](300)
val foldedUnsigned : ubyte = truncatingCast[ubyte](-1)
val directFloat : float = 0.1f
val exponentFloat : float = 1e3f

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
  std::string verification_error;
  llvm::raw_string_ostream verification_output{verification_error};
  expect(!llvm::verifyModule(*module, &verification_output),
         "numeric conversion IR passes llvm::verifyModule: " +
             verification_output.str());
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
  expect(ir.find("float 1.000000e+03") != std::string::npos,
         "scientific f-suffixed literal is represented as float");

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
      "enum NumericCastError { Overflow, Underflow, IncompatibleSign, "
      "NonFinite, FractionalLoss, PrecisionLoss } "
      "enum Result[T, E] { Success(T), Failure(E) } "
      "def main() : int { var source : long = 42 "
      "val value : Result[int, NumericCastError] = "
      "checkedCast[int](source) return 0 }",
      "checkedCast requires Result to define Ok(T) and Error(E)");
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

  struct DomainCase {
    std::string_view source;
    std::string_view destination;
    bool lossy_on_32_bit;
    bool lossy_on_64_bit;
  };
  const std::vector<DomainCase> domain_cases{
      {"short", "float", false, false},
      {"ushort", "float", false, false},
      {"int", "float", true, true},
      {"uint", "float", true, true},
      {"long", "float", true, true},
      {"ulong", "float", true, true},
      {"isize", "float", true, true},
      {"usize", "float", true, true},
      {"int", "double", false, false},
      {"uint", "double", false, false},
      {"long", "double", true, true},
      {"ulong", "double", true, true},
      {"isize", "double", false, true},
      {"usize", "double", false, true},
  };
  for (const DomainCase &item : domain_cases) {
    const std::string cast_source =
        "def main() : int { val source : " + std::string{item.source} +
        " = " + std::string{item.source} + "(1) val target : " +
        std::string{item.destination} + " = " +
        std::string{item.destination} + "(source) return 0 }";
    for (const std::uint32_t pointer_width : {32U, 64U}) {
      const bool expected = pointer_width == 32 ? item.lossy_on_32_bit
                                                 : item.lossy_on_64_bit;
      const std::size_t warnings = lossy_cast_warning_count(
          cast_source,
          {.target = {.triple = pointer_width == 32
                                    ? "i686-unknown-linux-gnu"
                                    : "x86_64-unknown-linux-gnu",
                      .pointer_width = pointer_width}});
      expect(warnings == (expected ? 1U : 0U),
             std::string{item.source} + " to " +
                 std::string{item.destination} + " on " +
                 std::to_string(pointer_width) +
                 "-bit targets follows the complete integer domain");
    }
  }

  expect(lossy_cast_warning_count(R"(
def main() : int {
    val exactFloatBoundary : int = 16777216
    val adjacentFloatValue : int = 16777217
    val exactAsFloat : float = float(exactFloatBoundary)
    val adjacentAsFloat : float = float(adjacentFloatValue)
    val two : long = long(2)
    val exactDoubleBoundary : long = two << usize(52)
    val adjacentDoubleValue : long = exactDoubleBoundary + long(1)
    val exactAsDouble : double = double(exactDoubleBoundary)
    val adjacentAsDouble : double = double(adjacentDoubleValue)
    return 0
}
)") == 4,
         "integer variables at and above 2^24/2^53 report JANA0013");
  expect(lossy_cast_warning_count(R"(
def main() : int {
    val contextual : float = float(16777217)
    return 0
}
)") == 0,
         "contextual integer literal casts keep their existing behavior");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "numeric conversion policies are typed and backend-stable\n";
  return 0;
}
