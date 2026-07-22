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

std::string emit_ir(std::string_view source) {
  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "integer_boundary");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  return ir;
}

void expect_compile_error(std::string_view source,
                          std::string_view expected_message) {
  try {
    static_cast<void>(emit_ir(source));
    expect(false, "invalid integer boundary source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "integer boundary error contains the expected explanation");
  }
}

void expect_compiles(std::string_view source, std::string_view message) {
  try {
    static_cast<void>(emit_ir(source));
  } catch (const janus::CompileError &error) {
    std::cerr << "FAILED: " << message << ": " << error.what() << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  const std::string ir = emit_ir(R"(
def printByte(value : byte) : Unit {
    println(value)
}

def main() : int {
    val minimum : int = -2147483648
    val wrapped : int = -minimum
    val divisor : int = -1
    val quotient : int = minimum / divisor
    val remainder : int = minimum % divisor
    val unsignedDividend : usize = usize(0) - usize(1)
    val unsignedDivisor : usize = usize(3)
    val unsignedQuotient : usize = unsignedDividend / unsignedDivisor
    val unsignedRemainder : usize = unsignedDividend % unsignedDivisor
    return wrapped + quotient + remainder + int(unsignedQuotient) +
        int(unsignedRemainder)
}
)");

  expect(ir.find("i32 -2147483648") != std::string::npos,
         "the signed i32 minimum literal is representable");
  expect(ir.find("sub i32 0") != std::string::npos,
         "unary minus keeps wrapping LLVM semantics");
  expect(ir.find("add nsw") == std::string::npos &&
             ir.find("sub nsw") == std::string::npos &&
             ir.find("mul nsw") == std::string::npos &&
             ir.find("add nuw") == std::string::npos &&
             ir.find("sub nuw") == std::string::npos &&
             ir.find("mul nuw") == std::string::npos,
         "integer arithmetic is emitted without LLVM overflow flags");
  expect(ir.find("sdiv i32") != std::string::npos,
         "signed division remains signed on the valid path");
  expect(ir.find("srem i32") != std::string::npos,
         "signed remainder remains signed on the valid path");
  expect(ir.find("udiv i64") != std::string::npos,
         "usize division remains unsigned on the valid path");
  expect(ir.find("urem i64") != std::string::npos,
         "usize remainder remains unsigned on the valid path");
  expect(ir.find("call void @janus_panic") != std::string::npos,
         "integer division and remainder lower deterministic trap guards");
  expect(ir.find("sext i8") != std::string::npos &&
             ir.find("declare void @janus_print_byte(i32)") !=
                 std::string::npos,
         "byte output crosses the runtime ABI as a signed i32 value");

  expect_compile_error("def main() : int { val value : int = 2147483648 "
                       "return value }",
                       "integer literal is outside the signed 32-bit range");
  expect_compile_error("def main() : int { val value : int = -2147483649 "
                       "return value }",
                       "integer literal is outside the signed 32-bit range");
  expect_compiles("def main() : int { val value : int = --2147483648 "
                  "return value }",
                  "nested unary minus preserves the signed int minimum boundary");
  expect_compiles("def main() : int { val value : int = ---2147483648 "
                  "return value }",
                  "triple unary minus preserves the signed int minimum boundary");
  expect_compile_error("def main() : int { val value : int = -(2147483648) "
                       "return value }",
                       "integer literal is outside the signed 32-bit range");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "integer boundaries are parsed and lowered deterministically\n";
  return 0;
}
