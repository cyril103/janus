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

  expect(ir.find("call void @janus_panic_with_context") != std::string::npos,
         "panic delegates source-aware termination to the runtime");
  expect(ir.find("declare void @janus_panic_with_context(ptr, i64, ptr, i32, "
                 "ptr, i32)") != std::string::npos,
         "panic uses the portable context runtime ABI");
  expect(ir.find("i32 4, ptr @panic.function, i32 2") != std::string::npos,
         "panic forwards its source line and full trace mode");
  expect(ir.find("requirePositive\\00") != std::string::npos,
         "panic forwards its enclosing function");
  expect(ir.find("call void @requirePositive(i32 42)") != std::string::npos,
         "runtime checks can be called as Unit functions");

  expect_compile_error("def main() : int { panic(1) return 0 }",
                       "where type 'string' is required");
  expect_compile_error("def main() : int { panic() return 0 }",
                       "expects one string argument");
  expect_compile_error("def main() : int { panic[int](\"failure\") return 0 }",
                       "no type argument");

  llvm::LLVMContext short_context;
  janus::backend::llvm::IrGenerator short_generator{short_context};
  const std::unique_ptr<llvm::Module> short_module = short_generator.generate(
      program, "panic", janus::backend::llvm::PanicTraceMode::Short);
  std::string short_ir;
  llvm::raw_string_ostream short_output{short_ir};
  short_module->print(short_output, nullptr);
  short_output.flush();
  expect(short_ir.find("ptr @panic.function, i32 1") != std::string::npos,
         "short panic traces are encoded in the runtime call");

  llvm::LLVMContext off_context;
  janus::backend::llvm::IrGenerator off_generator{off_context};
  const std::unique_ptr<llvm::Module> off_module = off_generator.generate(
      program, "panic", janus::backend::llvm::PanicTraceMode::Off);
  std::string off_ir;
  llvm::raw_string_ostream off_output{off_ir};
  off_module->print(off_output, nullptr);
  off_output.flush();
  expect(off_ir.find("ptr @panic.function, i32 0") != std::string::npos,
         "panic traces can be disabled");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "panic emits a message and terminates the process\n";
  return 0;
}
