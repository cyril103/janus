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
    expect(false, "invalid output call must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "output error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def main() : int {
    print("answer: ")
    println(42)
    println(byte(-7))
    println(usize(12))
    println(3.5)
    println('λ')
    println(true)
    return 0
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "standard_output");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("call void @janus_write_stdout") != std::string::npos,
         "strings are written to stdout");
  expect(ir.find("call void @janus_print_int") != std::string::npos &&
             ir.find("call void @janus_print_double") != std::string::npos,
         "numbers are formatted on stdout");
  expect(ir.find("call void @janus_print_char(i32") != std::string::npos,
         "Unicode characters use the portable runtime helper");

  expect_compile_error("def main() : int { print() return 0 }",
                       "expects one printable argument");
  expect_compile_error(
      "class Box() {} def main() : int { val box : Box = new Box() "
      "print(box) delete box return 0 }",
      "supports int, double, byte, char, bool, string, and usize");
  expect_compile_error("def main() : int { println[int](1) return 0 }",
                       "no type argument");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "print and println write primitive values to stdout\n";
  return 0;
}
