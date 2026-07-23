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
    expect(false, "invalid primitive operation must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "primitive operation error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def main() : int {
    val ten : int = 10
    val three : int = 3
    val two : int = 2
    val result : int = ten + three * two
    val remainder : int = result % 5
    val negative : int = -remainder
    val minimum : byte = -128
    val first : byte = 7
    val second : byte = first + first
    val unsignedFirst : ubyte = 250
    val unsignedSecond : ubyte = 10
    val unsignedWrap : ubyte = unsignedFirst + unsignedSecond
    val unsignedOrder : bool = unsignedFirst > unsignedSecond
    val onePointFive : double = 1.5
    val twoPointZero : double = 2.0
    val floating : double = onePointFive * twoPointZero + 1.0
    val ordered : bool = result >= 16 && remainder == 1
    val textEqual : bool = "Janus" == "Janus"
    val textDifferent : bool = "a" != "b"
    val firstCharacter : char = 'a'
    val secondCharacter : char = 'b'
    val characters : bool = firstCharacter < secondCharacter
    val logical : bool = !false || false
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  const auto &declaration =
      std::get<janus::ast::ValueDeclaration>(program.functions[0].body[3]);
  const auto *addition = std::get_if<janus::ast::BinaryExpression>(
      &declaration.initializer->value);
  expect(addition != nullptr &&
             addition->operation == janus::ast::BinaryOperator::Add,
         "addition is the root of 10 + 3 * 2");
  const auto *multiplication =
      addition == nullptr
          ? nullptr
          : std::get_if<janus::ast::BinaryExpression>(&addition->right->value);
  expect(multiplication != nullptr &&
             multiplication->operation == janus::ast::BinaryOperator::Multiply,
         "multiplication has precedence over addition");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "primitive_operations");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("mul i32") != std::string::npos,
         "integer multiplication is emitted");
  expect(ir.find("srem i32") != std::string::npos,
         "signed integer remainder is emitted");
  expect(ir.find("add i8") != std::string::npos,
         "byte arithmetic keeps its 8-bit representation");
  expect(ir.find("icmp ugt i8") != std::string::npos,
         "ubyte comparison is unsigned");
  expect(ir.find("fmul double") != std::string::npos,
         "double multiplication is emitted");
  expect(ir.find("icmp sge i32") != std::string::npos,
         "signed integer comparison is emitted");
  expect(ir.find("icmp ult i32") != std::string::npos,
         "char comparison is unsigned");
  expect(ir.find("phi i1") != std::string::npos,
         "logical and string operations use control-flow results");
  expect(ir.find("call i32 @janus_memcmp") != std::string::npos,
         "string equality compares UTF-8 bytes");
  expect(ir.find("add nsw") == std::string::npos &&
             ir.find("add nuw") == std::string::npos,
         "integer arithmetic keeps modulo overflow semantics");

  expect_compile_error("def main() : int { val x : double = 1 + 2.0 return 0 }",
                       "operands must have the same type");
  expect_compile_error(
      "def main() : int { val x : int = true + false return 0 }",
      "arithmetic operators require");
  expect_compile_error(
      "def main() : int { val x : double = 4.0 % 2.0 return 0 }",
      "operator '%' requires");
  expect_compile_error(
      "def main() : int { val x : ubyte = 256 return 0 }",
      "integer literal is outside the unsigned 8-bit range");
  expect_compile_error(
      "def main() : int { val x : ubyte = -1 return 0 }",
      "integer literal is outside the unsigned 8-bit range");
  expect_compile_error(
      "def main() : int { val x : bool = \"a\" < \"b\" return 0 }",
      "comparison operators require");
  expect_compile_error("def main() : int { val x : bool = 1 && 2 return 0 }",
                       "logical operators require");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "primitive operations are typed and lowered to LLVM IR\n";
  return 0;
}
