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
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
    expect(false, "invalid constant program must fail");
  } catch (const janus::CompileError &error) {
    if (std::string_view{error.what()}.find(expected_message) ==
        std::string_view::npos) {
      std::cerr << "FAILED: expected diagnostic containing '"
                << expected_message << "', got '" << error.what() << "'\n";
      ++failures;
    }
  }
}

} // namespace

int main() {
  janus::frontend::Parser parser{R"(
const width : int = 80
const height : int = 25
const capacity : int = width * height
const selected : int = if capacity == 2000 { 7 } else { 9 }

const def align(value : usize, boundary : usize) : usize {
    return ((value + boundary - usize(1)) / boundary) * boundary
}

const bufferSize : usize = align(usize(1000), usize(64))
staticAssert(capacity == 2000)
staticAssert(bufferSize == usize(1024), "alignment must remain stable")

def main() : int {
    val runtime : usize = align(usize(5), usize(4))
    var compatibility : int = selected
    return int(runtime) + compatibility
}
)"};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "compile_time_constants");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("__janus_global_entry__capacity") == std::string::npos,
         "a scalar const does not allocate runtime global storage");
  expect(ir.find("__janus_global_entry__bufferSize") == std::string::npos,
         "a const-def result does not allocate runtime global storage");
  expect(ir.find("define") != std::string::npos &&
             ir.find("align") != std::string::npos,
         "const def remains callable at runtime");
  expect(ir.find("ret i32 15") == std::string::npos,
         "ordinary val/var execution is not globally folded away");

  expect_compile_error(
      "var runtime : int = 1\nconst invalid : int = runtime\n"
      "def main() : int { return 0 }",
      "constant 'invalid' cannot depend on mutable global 'runtime'");
  expect_compile_error(
      "const first : int = second\nconst second : int = first\n"
      "def main() : int { return 0 }",
      "cyclic constant definition");
  expect_compile_error(
      "const invalid : byte = 127 + 1\ndef main() : int { return 0 }",
      "constant integer expression overflows type 'byte'");
  expect_compile_error(
      "const invalid : int = 1 / 0\ndef main() : int { return 0 }",
      "division by zero in constant expression");
  expect_compile_error(
      "def runtime() : bool { return true }\nstaticAssert(runtime())\n"
      "def main() : int { return 0 }",
      "static assertion condition is not a constant expression");
  expect_compile_error(
      "staticAssert(1 == 2, \"numbers disagree\")\n"
      "def main() : int { return 0 }",
      "static assertion failed: numbers disagree");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "compile-time constants are deterministic and storage-free\n";
  return 0;
}
