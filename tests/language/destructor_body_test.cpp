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
    expect(false, "invalid destructor source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "destructor error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Leaf(val value : int) {
    destructor {
    }
}
class Owner(private val leaf : Leaf) {
    destructor {
        delete leaf
    }
}
def main() : int {
    val leaf : Leaf = new Leaf(42)
    val owner : Owner = new Owner(leaf)
    delete owner
    return 42
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.functions.contains("Owner.destructor"),
         "the destructor body is semantically analyzed");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "destructor_body");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  const std::size_t destructor = ir.find("define internal void "
                                         "@Owner__destructor(ptr %this)");
  expect(destructor != std::string::npos,
         "a destructor is emitted as an internal void function");
  expect(destructor != std::string::npos &&
             ir.find("call void @Leaf__destructor", destructor) !=
                 std::string::npos,
         "a destructor body can delete an owned field");
  expect(destructor != std::string::npos &&
             ir.find("call void @free", destructor) != std::string::npos,
         "delete in a destructor releases the nested object");

  expect_compile_error("class Invalid() { destructor { return 1 } } "
                       "def main() : int { return 0 }",
                       "cannot return a value");
  expect_compile_error("class Invalid() { destructor { unknown() } } "
                       "def main() : int { return 0 }",
                       "unknown function 'unknown'");
  expect_compile_error(
      "class Invalid() { var value : int destructor { return value } } "
      "def main() : int { return 0 }",
      "cannot return a value");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "destructor bodies are analyzed and emitted\n";
  return 0;
}
