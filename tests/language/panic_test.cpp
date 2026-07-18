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
    expect(false, "invalid panic source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "panic error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def requirePositive(value : int) : Unit {
    if value < 0 {
        panic("value must be positive\n")
    }
}
def main() : int {
    requirePositive(42)
    return 42
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "panic");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("call i64 @write(i32 2") != std::string::npos,
         "panic writes its UTF-8 message to stderr");
  expect(ir.find("call void @abort()") != std::string::npos,
         "panic terminates through abort");
  expect(ir.find("call void @requirePositive(i32 42)") != std::string::npos,
         "runtime checks can be called as Unit functions");

  expect_compile_error("def main() : int { panic(1) return 0 }",
                       "where type 'string' is required");
  expect_compile_error("def main() : int { panic() return 0 }",
                       "expects one string argument");
  expect_compile_error("def main() : int { panic[int](\"failure\") return 0 }",
                       "no type argument");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "panic emits a message and terminates the process\n";
  return 0;
}
