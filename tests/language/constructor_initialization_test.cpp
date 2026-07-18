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
    expect(false, "invalid constructor source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "constructor error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Counter(initialValue : int, private val step : int) {
    private val initial : int = initialValue
    private var current : int = initial
    def next() : int {
        current = current + step
        return current
    }
}
def main() : int {
    val counter : Counter = new Counter(40, 2)
    val result : int = counter.next()
    delete counter
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  const auto &counter = program.classes.front();
  expect(counter.constructor_parameters.size() == 1,
         "a plain constructor parameter is parsed");
  expect(counter.constructor_parameters.front().name == "initialValue",
         "the plain parameter keeps its name");
  expect(counter.constructor_fields.size() == 1,
         "val constructor parameters remain fields");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "constructor_initialization");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("%class.Counter = type { i32, i32, i32 }") !=
             std::string::npos,
         "plain parameters do not occupy object storage");
  expect(ir.find("%initialValue.constructor = alloca i32") != std::string::npos,
         "plain parameters are available during initialization");
  expect(ir.find("initialValue.value") != std::string::npos,
         "a field initializer reads a constructor parameter");
  expect(ir.find("initial.value") != std::string::npos,
         "field initializers run in declaration order");

  expect_compile_error(
      "class Value(seed : int) { val stored : int = seed } "
      "def main() : int { val value : Value = new Value() return 0 }",
      "expects 1 argument");
  expect_compile_error(
      "class Value(seed : bool) { val stored : int = seed } "
      "def main() : int { val value : Value = new Value(true) return 0 }",
      "expression of type 'bool'");
  expect_compile_error(
      "class Value(seed : int) { var first : int "
      "val second : int = first } "
      "def main() : int { val value : Value = new Value(1) return 0 }",
      "used before initialization");
  expect_compile_error("class Value(seed : int) { val stored : int = seed } "
                       "def main() : int { val value : Value = new Value(1) "
                       "return value.seed }",
                       "has no field 'seed'");
  expect_compile_error("class Invalid(val field : int, plain : int) {} "
                       "def main() : int { return 0 }",
                       "must precede val/var fields");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "constructors initialize fields from transient parameters\n";
  return 0;
}
