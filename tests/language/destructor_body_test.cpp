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

  janus::frontend::Parser defer_parser{
      "enum Option[T] { Some(T), None } "
      "class Resource() {} def cleanup() : Unit {} "
      "def run() : Unit { val resource : Resource = new Resource() "
      "defer delete resource defer cleanup() } "
      "def early() : int { val resource : Resource = new Resource() "
      "defer delete resource return 7 } "
      "def attempt(value : Option[int]) : Option[int] { "
      "val resource : Resource = new Resource() defer delete resource "
      "val result : int = value? return Option.Some[int](result) } "
      "def main() : int { run() return 0 }"};
  const janus::ast::Program defer_program = defer_parser.parse_program();
  expect(std::holds_alternative<janus::ast::DeferStatement>(
             defer_program.functions[1].body[1]),
         "defer delete is represented explicitly in the AST");
  expect(std::holds_alternative<janus::ast::DeferStatement>(
             defer_program.functions[1].body[2]),
         "deferred calls are represented explicitly in the AST");
  static_cast<void>(analyzer.analyze(defer_program));
  llvm::LLVMContext defer_context;
  janus::backend::llvm::IrGenerator defer_generator{defer_context};
  const std::unique_ptr<llvm::Module> defer_module =
      defer_generator.generate(defer_program, "defer_scope");
  std::string defer_ir;
  llvm::raw_string_ostream defer_output{defer_ir};
  defer_module->print(defer_output, nullptr);
  defer_output.flush();
  const std::size_t run_function = defer_ir.find("define void @run()");
  const std::size_t cleanup_call =
      defer_ir.find("call void @cleanup()", run_function);
  const std::size_t resource_cleanup =
      defer_ir.find("call void @Resource__destructor", run_function);
  expect(cleanup_call != std::string::npos &&
             resource_cleanup != std::string::npos &&
             cleanup_call < resource_cleanup,
         "normal scope exit executes deferred actions in LIFO order");
  const std::size_t early_function = defer_ir.find("define i32 @early()");
  const std::size_t early_cleanup =
      defer_ir.find("call void @Resource__destructor", early_function);
  const std::size_t early_return = defer_ir.find("ret i32 7", early_function);
  expect(early_cleanup != std::string::npos &&
             early_return != std::string::npos && early_cleanup < early_return,
         "return executes active deferred actions before leaving");
  const std::size_t attempt_function =
      defer_ir.find("define %enum.Option__int @attempt");
  const std::size_t try_failure =
      defer_ir.find("try.failure:", attempt_function);
  const std::size_t failure_cleanup =
      defer_ir.find("call void @Resource__destructor", try_failure);
  const std::size_t failure_return =
      defer_ir.find("ret %enum.Option__int", try_failure);
  expect(try_failure != std::string::npos &&
             failure_cleanup != std::string::npos &&
             failure_return != std::string::npos &&
             failure_cleanup < failure_return,
         "operator ? executes active deferred actions on propagation");

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
  expect_compile_error("def main() : int { defer 1 return 0 }",
                       "defer requires delete");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "val resource : Resource = new Resource() defer delete resource "
      "delete resource return 0 }",
      "scheduled for deferred cleanup");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "val resource : Resource = new Resource() defer delete resource "
      "val moved : Resource = move resource delete moved return 0 }",
      "scheduled for deferred cleanup");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "val resource : Resource = new Resource() defer delete resource "
      "defer delete resource return 0 }",
      "already scheduled for deferred cleanup");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "var resource : Resource = new Resource() defer delete resource "
      "resource = new Resource() return 0 }",
      "scheduled for deferred cleanup");
  expect_compile_error(
      "class Resource() { consume def close() : Unit { delete this } } "
      "def main() : int { val resource : Resource = new Resource() "
      "defer delete resource resource.close() return 0 }",
      "scheduled for deferred cleanup");
  expect_compile_error(
      "class Resource() { consume def close() : Unit { delete this } } "
      "def main() : int { val resource : Resource = new Resource() "
      "defer resource.close() delete resource return 0 }",
      "scheduled for deferred cleanup");
  expect_compile_error(
      "enum Option[T] { Some(T), None } def cleanup(value : int) : Unit {} "
      "def run(value : Option[int]) : Option[int] { "
      "defer cleanup(value?) return Option.Some[int](0) } "
      "def main() : int { return 0 }",
      "operator '?' is not supported in deferred actions");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "destructor bodies are analyzed and emitted\n";
  return 0;
}
