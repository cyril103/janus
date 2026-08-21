#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void expect_valid(std::string_view source, bool generate_ir = false) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    if (generate_ir) {
      llvm::LLVMContext context;
      janus::backend::llvm::IrGenerator generator{context};
      expect(generator.generate(program, "borrowed_calls_closures") != nullptr,
             "borrowed calls and bounded closures generate LLVM IR");
    }
  } catch (const std::exception &error) {
    std::cerr << "FAILED: valid source was rejected: " << error.what() << '\n';
    ++failures;
  }
}

void expect_compile_error(std::string_view source,
                          std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
    expect(false, "escaping borrowed closure must be rejected");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           expected_message);
    expect(error.diagnostic().code != janus::DiagnosticCode::Unclassified,
           "borrowed call errors use structured diagnostic codes");
  }
}

} // namespace

int main() {
  expect_valid(R"(
class Counter(var value : int) {
  borrow def read() : int { return value }
  def add(amount : int) : Unit { value = value + amount }
}
def observe(borrow counter : Counter) : int { return counter.read() }
def forward(borrow counter : Counter) : int { return observe(counter) }
def increment(borrow var counter : Counter) : Unit { counter.add(1) }
def forwardMutable(borrow var counter : Counter) : int {
  increment(counter)
  return observe(counter)
}
def main() : int {
  val counter : Counter = new Counter(1)
  val first : int = forward(counter)
  val second : int = forwardMutable(counter)
  if true {
    borrow val view : Counter = counter
    val reader : () => int = () => view.read()
    println(reader())
    delete reader
  }
  if true {
    borrow var editable : Counter = counter
    val edit : () => Unit = () => editable.add(2)
    edit()
    delete edit
  }
  delete counter
  return first + second
}
)",
               true);

  expect_compile_error(
      R"(
class Counter(val value : int) {
  borrow def read() : int { return value }
}
def escape(borrow counter : Counter) : () => int {
  return () => counter.read()
}
def main() : int { return 0 }
)",
      "closure captures borrowed value 'counter' and cannot escape by return");

  expect_compile_error(R"(
class Counter(val value : int) {
  borrow def read() : int { return value }
}
def receive(callback : () => int) : Unit { delete callback }
def test(borrow counter : Counter) : Unit {
  receive(() => counter.read())
}
def main() : int { return 0 }
)",
                       "receiving call may store or return it");

  expect_compile_error(R"(
class Counter(var value : int) {
  def add(amount : int) : Unit { value = value + amount }
}
def test(borrow counter : Counter) : Unit {
  val edit : () => Unit = () => counter.add(1)
  delete edit
}
def main() : int { return 0 }
)",
                       "can only call a borrow method");

  expect_compile_error(R"(
class Counter(val value : int) {
  borrow def read() : int { return value }
}
class CallbackHolder(val callback : () => int) {}
def test(borrow counter : Counter) : Unit {
  val holder : CallbackHolder =
  new CallbackHolder(() => counter.read())
  delete holder
}
def main() : int { return 0 }
)",
                       "receiving call may store or return it");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "borrowed calls and closure captures remain bounded\n";
  return 0;
}
