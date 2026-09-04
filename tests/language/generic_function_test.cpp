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
    expect(false, "invalid generic source must produce a compile error");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "generic compile error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def identity[T](value : T) : T {
    return move value
}

def forward[T](value : T) : T {
    return identity(identity(move value))
}

struct Box[T](val value : T) derives Copy {}
class Resource() {}

def unbox[T <: Copy](value : Box[T]) : T {
    return value.value
}

def create[T](ignored : int) : Ptr[T] {
    return null[T]()
}

def accept(value : int) : int { return value }

def main() : int {
    val integer : int = identity[int](5)
    val floating : double = identity[double](2.5)
    val message : string = identity[string]("Janus")
    val inferred = identity(6)
    val nestedCall = accept(identity(identity(7)))
    val forwarded = forward(8)
    val boxed : Box[int] = new Box[int](7)
    val nestedBox = unbox(boxed)
    val resource : Resource = new Resource()
    val moved = identity(move resource)
    delete moved
    val pointer : Ptr[int] = create(0)
    delete pointer
    return integer + inferred + nestedCall + nestedBox + forwarded
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.functions.size() == 6, "six functions are parsed");
  expect(program.functions.front().type_parameters.size() == 1,
         "identity declares one type parameter");
  expect(program.functions.front().type_parameters.front() == "T",
         "the type parameter is named T");
  expect(program.functions.front().parameters.size() == 1,
         "identity declares one value parameter");
  expect(program.functions.front().parameters.front().type.name == "T",
         "the value parameter uses T");
  expect(program.functions.front().return_type.name == "T",
         "identity returns T");

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(!analysis.functions.at("identity").at("value").type.is_concrete(),
         "identity parameter remains symbolic during generic analysis");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "generics");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("define i32 @identity__int(i32") != std::string::npos,
         "identity[int] is monomorphized with i32");
  expect(ir.find("define double @identity__double(double") != std::string::npos,
         "identity[double] is monomorphized with double");
  expect(ir.find("call i32 @identity__int(i32 5)") != std::string::npos,
         "main calls the int specialization");
  expect(ir.find("call i32 @identity__int(i32 6)") != std::string::npos,
         "call arguments fully constrain an omitted generic type argument");
  expect(ir.find("call i32 @accept(i32 %identity.result") != std::string::npos,
         "an inferred generic return is canonical inside another call");
  expect(ir.find("define i32 @forward__int(i32") != std::string::npos,
         "nested inference preserves an enclosing generic substitution");
  expect(ir.find("call i32 @unbox__int(") != std::string::npos,
         "nested generic argument types constrain omitted type arguments");
  expect(ir.find("call double @identity__double(double 2.500000e+00)") !=
             std::string::npos,
         "main calls the double specialization");
  expect(ir.find("define { ptr, i64 } @identity__string({ ptr, i64 }") !=
             std::string::npos,
         "identity[string] is monomorphized with the string structure");

  janus::frontend::Parser warning_parser{
      "def identity[T](value : T) : T { return move value } "
      "def main() : int { val inferred = identity(int(2.5)) return inferred }"};
  const janus::semantic::AnalysisResult warning_analysis =
      analyzer.analyze(warning_parser.parse_program());
  std::size_t lossy_cast_warnings = 0;
  for (const janus::Diagnostic &diagnostic : warning_analysis.diagnostics)
    if (diagnostic.code == janus::DiagnosticCode::AnalyzerLossyNumericCast)
      ++lossy_cast_warnings;
  expect(lossy_cast_warnings == 1,
         "speculative generic inference does not duplicate diagnostics");

  expect_compile_error("def choose[T](x : int) : int { return x } "
                       "def main() : int { return choose(1) }",
                       "is not constrained by call arguments");
  expect_compile_error("def identity[T](x : T) : T { return move x } "
                       "def main() : int { return identity[bool](1) }",
                       "expression of type 'int'");
  expect_compile_error("def identity[T](x : T) : T { return move x } "
                       "def main() : int { return identity[missing](1) }",
                       "unknown type 'missing'");
  expect_compile_error("def duplicate[T, T](x : T) : T { return x } "
                       "def main() : int { return 0 }",
                       "type parameter 'T' is already declared");
  expect_compile_error("def main() : int { return missing }",
                       "unknown value 'missing'");
  expect_compile_error(
      "def invoke[T](callback : () => T) : T { return callback() } "
      "def main() : int { val outer = () => { "
      "val value = invoke(() => { return missing }) return value } "
      "delete outer return 0 }",
      "unknown value 'missing'");
  janus::frontend::Parser copy_parser{
      "def duplicate[T <: Copy](value : T) : T { return value } "
      "struct Pair(val left : int, val right : int) {} "
      "def main() : int { val pair : Pair = new Pair(1, 2) "
      "val copied : Pair = duplicate[Pair](pair) "
      "return duplicate[int](copied.left + copied.right) }"};
  static_cast<void>(analyzer.analyze(copy_parser.parse_program()));
  expect_compile_error(
      "def duplicate[T <: Copy](value : T) : T { return value } "
      "class Resource() {} def main() : int { "
      "val resource : Resource = new Resource() "
      "duplicate[Resource](resource) delete resource return 0 }",
      "does not satisfy constraint 'Copy'");
  expect_compile_error(
      "def duplicate[T <: Copy](value : T) : T { return value } "
      "class Resource() {} struct Owned(val resource : Resource) {} "
      "def main() : int { val owned : Owned = new Owned(new Resource()) "
      "val copied : Owned = duplicate[Owned](move owned) "
      "delete copied return 0 }",
      "does not satisfy constraint 'Copy'");
  expect_compile_error("trait Copy {} def main() : int { return 0 }",
                       "trait 'Copy' is intrinsic");
  expect_compile_error(
      "class Store[T <: Copy]() {} class Resource() {} "
      "struct Owned(val resource : Resource) {} def main() : int { "
      "val store : Store[Owned] = new Store[Owned]() "
      "delete store return 0 }",
      "does not satisfy constraint 'Copy'");
  janus::frontend::Parser capability_parser{
      "struct Token(val value : int) derives Equality, Debug {} "
      "def same[T <: Equality & Debug](left : T, right : T) : bool { "
      "debug(left) return left == right } "
      "def main() : int { val first : Token = new Token(1) "
      "val second : Token = new Token(1) "
      "return if same[Token](first, second) { 0 } else { 1 } }"};
  static_cast<void>(analyzer.analyze(capability_parser.parse_program()));
  expect_compile_error(
      "class Resource() {} "
      "def same[T <: Equality](left : T, right : T) : bool { "
      "return left == right } "
      "def main() : int { val value : Resource = new Resource() "
      "val result : bool = same[Resource](value, value) delete value "
      "return if result { 0 } else { 1 } }",
      "does not satisfy constraint 'Equality'");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "generic identity function is monomorphized for each type\n";
  return 0;
}
