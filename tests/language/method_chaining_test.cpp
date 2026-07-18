#include "janus/backend/llvm/ir_generator.hpp"
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

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Counter(var value : int) {
    def add(amount : int) : Counter {
        value = value + amount
        return this
    }

    def self() : Counter {
        return this
    }
}

def main() : int {
    val counter : Counter = new Counter(1)
    val result : int = counter.self().add(2).add(3).value
    delete counter
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "method_chaining");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("call ptr @Counter__self") != std::string::npos,
         "a method can be called on a method result");
  expect(ir.find("call ptr @Counter__add") != std::string::npos,
         "multiple method calls can be chained");
  expect(ir.find("value.value = load i32") != std::string::npos,
         "a field can be read from a temporary receiver");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "method calls and field accesses can be chained\n";
  return 0;
}
