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
    expect(false, "invalid usize source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "usize error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def main() : int {
    val source : int = 5
    val limit : usize = usize(source)
    val literal : usize = 1
    var index : usize = usize(0)
    var total : usize = usize(0)
    while index < limit {
        total = total + index
        index = index + usize(1)
    }
    val divisor : usize = usize(2)
    val quotient : usize = total / divisor
    return int(total)
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.functions.at("main").at("limit").type.name() == "usize",
         "usize is retained in the semantic symbol table");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "usize");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("sext i32") != std::string::npos,
         "signed int to usize is an explicit extension");
  expect(ir.find("trunc i64") != std::string::npos,
         "usize to int is an explicit truncation");
  expect(ir.find("icmp ult i64") != std::string::npos,
         "usize comparisons are unsigned");
  expect(ir.find("udiv i64") != std::string::npos,
         "usize division is unsigned");

  expect_compile_error(
      "def main() : int { val size : usize = usize(\"12\") return 0 }",
      "cannot explicitly cast type 'string' to 'usize'");
  expect_compile_error(
      "def main() : int { val size : usize = usize(1, 2) return 0 }",
      "expects exactly one argument");
  expect_compile_error("def main() : int { val size : usize = usize(1) "
                       "val invalid : usize = -size return 0 }",
                       "unary operator '-' requires");
  expect_compile_error("def main() : int { val size : usize = usize(1) "
                       "val invalid : usize = size + 1 return 0 }",
                       "operands must have the same type");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "usize uses explicit conversions and unsigned LLVM operations\n";
  return 0;
}
