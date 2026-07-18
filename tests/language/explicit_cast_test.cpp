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
    expect(false, "invalid explicit cast must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "cast error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Source() {}
class Target() {}

def main() : int {
    val original : double = 12.75
    val truncated : int = int(original)
    val floating : double = double(truncated)
    val truth : bool = bool(floating)
    val codepoint : char = char(955)
    val small : byte = byte(codepoint)

    val source : Source = new Source()
    val raw : Ptr[byte] = Ptr[byte](source)
    val narrowedAddress : int = int(raw)
    val present : bool = bool(raw)
    val address : usize = usize(raw)
    val pointerFromInt : Ptr[byte] = Ptr[byte](narrowedAddress)
    val integers : Ptr[int] = Ptr[int](address)
    val target : Target = Target(integers)

    delete source
    return int(truth) + int(small)
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "explicit_cast");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("fptosi double") != std::string::npos,
         "double can be explicitly cast to a signed integer");
  expect(ir.find("sitofp i32") != std::string::npos,
         "signed integers can be explicitly cast to double");
  expect(ir.find("fcmp une double") != std::string::npos,
         "double converts to bool by comparison with zero");
  expect(ir.find("ptrtoint ptr") != std::string::npos,
         "references can be explicitly cast to integers");
  expect(ir.find("inttoptr i64") != std::string::npos,
         "usize can be explicitly cast to a reference");
  expect(ir.find("icmp ne ptr") != std::string::npos,
         "references convert to bool through a null check");

  expect_compile_error(
      "def main() : int { val value : int = int(\"12\") return value }",
      "cannot explicitly cast type 'string' to 'int'");
  expect_compile_error(
      "def main() : int { val value : string = string(12) return 0 }",
      "cannot be used as an explicit cast target");
  expect_compile_error("def main() : int { val value : int = int() return 0 }",
                       "expects exactly one argument");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "explicit casts cover scalars and reference representations\n";
  return 0;
}
