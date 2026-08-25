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
    expect(false, "invalid first-class function program must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "function value error contains the expected explanation");
  }
}

} // namespace

int main() {
  expect_compile_error(
      "def main() : int { var data : Ptr[int] = alloc[int](usize(1)) "
      "val resize = () => { data = realloc[int](data, usize(2)) } "
      "delete resize free(data) return 0 }",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");
  expect_compile_error(
      "def main() : int { var data : Ptr[int] = alloc[int](usize(1)) "
      "val resize = () => { "
      "data = reallocPreserving[int](data, usize(2)) } "
      "delete resize free(data) return 0 }",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");
  expect_compile_error(
      "extern def release(consume data : Ptr[byte]) : Unit "
      "def main() : int { val pointer : Ptr[byte] = alloc[byte](usize(1)) "
      "val cleanup = () => { release(pointer) } delete cleanup "
      "free(pointer) return 0 }",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");
  expect_compile_error(
      "def main() : int { val pointer : Ptr[byte] = alloc[byte](usize(1)) "
      "val resized : Ptr[byte] = realloc[byte](pointer, usize(2)) "
      "val cleanup = () => { adoptReallocation[byte](pointer, resized) } "
      "delete cleanup free(pointer) free(resized) return 0 }",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");

  constexpr std::string_view source = R"(
class CallScope() {
    destructor {}
}

def apply[T](function : (T) => T, value : T) : T {
    val scope : CallScope = new CallScope()
    defer delete scope
    return function(value)
}

def makeAdder(amount : int) : (int) => int {
    return (value : int) => value + amount
}

def makeBlockAdder(amount : int) : (int) => int {
    return (value : int) => {
        val adjusted : int = value + amount
        if adjusted > 40 {
            return adjusted + 1
        }
        return adjusted
    }
}

def makeObserver() : (int) => Unit {
    return (value : int) => {
        val observed : int = value
        debug(observed)
    }
}

def makeConditionalObserver(condition : bool) : () => Unit {
    return () => {
        if condition { return }
        debug(1)
    }
}

def makeShadow(value : CallScope) : (int) => int {
    return (value : int) => value + 1
}

def makeNested(amount : int) : (int) => int {
    return (value : int) => {
        val inner = (nested : int) => {
            return nested + amount
        }
        val result : int = inner(value)
        delete inner
        return result
    }
}

def makeCounter(start : int) : () => int {
    var next : int = start
    return () => {
        next = next + 1
        return next
    }
}

def makeIdentity[T]() : (T) => T {
    return (value : T) => value
}

def main() : int {
    val increment : (int) => int = (value : int) => value + 1
    val first : int = apply[int](increment, 41)
    delete increment

    val addTen = makeAdder(10)
    val second = addTen(first)
    delete addTen

    val identity = makeIdentity[int]()
    val result = identity(second)
    delete identity

    val blockAdder = makeBlockAdder(1)
    val blockResult : int = blockAdder(result)
    delete blockAdder

    val observer = makeObserver()
    observer(blockResult)
    delete observer

    val conditionalObserver = makeConditionalObserver(false)
    conditionalObserver()
    delete conditionalObserver

    val nested = makeNested(2)
    val nestedResult : int = nested(blockResult)
    delete nested

    val counter = makeCounter(nestedResult)
    val counted : int = counter() + counter()
    delete counter
    return counted
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  const auto &main_body = program.functions.back().body;
  const auto &increment =
      std::get<janus::ast::ValueDeclaration>(main_body.front());
  expect(increment.declared_type.has_value() &&
             increment.declared_type->name == "Function" &&
             increment.declared_type->type_arguments.size() == 2,
         "the parser retains function signatures");
  expect(std::holds_alternative<janus::ast::LambdaExpression>(
             increment.initializer->value),
         "the parser retains lambda literals");
  const auto &block_lambda = std::get<janus::ast::LambdaExpression>(
      std::get<janus::ast::ReturnStatement>(
          program.functions[2].body.front()).expression->value);
  expect(std::holds_alternative<std::shared_ptr<janus::ast::LambdaBlock>>(
             block_lambda.body),
         "the parser retains lambda statement blocks");
  expect(std::get<std::shared_ptr<janus::ast::LambdaBlock>>(block_lambda.body)
                 ->statements.size() == 3,
         "lambda blocks contain ordinary Janus statements");

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.functions.at("main").at("increment").type.name() ==
             "(int) => int",
         "function values retain their semantic signature");
  expect(analysis.functions.at("main").at("blockAdder").type.name() ==
             "(int) => int",
         "block lambda return types are inferred from return statements");
  expect(analysis.functions.at("main").at("observer").type.name() ==
             "(int) => Unit",
         "a lambda block without return infers Unit");
  expect(analysis.functions.at("main").at("conditionalObserver").type.name() ==
             "() => Unit",
         "a partially returning lambda block infers Unit");
  expect(analysis.functions.at("main").at("nested").type.name() ==
             "(int) => int",
         "nested block lambdas retain inferred signatures and captures");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "first_class_function");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("%lambda.env.") != std::string::npos,
         "captured values are stored in closure environments");
  expect(ir.find("define internal i32 @__janus_lambda_") != std::string::npos,
         "lambda bodies lower to internal LLVM functions");
  expect(ir.find("adjusted") != std::string::npos,
         "multi-statement lambda bodies lower their locals and control flow");
  expect(ir.find("call i32 %") != std::string::npos,
         "function values are invoked through indirect calls");
  const std::size_t push_cleanup =
      ir.find("call void @janus_push_panic_cleanup");
  const std::size_t indirect_call = ir.find("call i32 %", push_cleanup);
  const std::size_t pop_cleanup =
      ir.find("call void @janus_pop_panic_cleanup", indirect_call);
  expect(push_cleanup != std::string::npos &&
             indirect_call != std::string::npos &&
             pop_cleanup != std::string::npos &&
             push_cleanup < indirect_call && indirect_call < pop_cleanup,
         "indirect calls register and unregister active caller cleanups");
  expect(ir.find("define internal void @__janus_panic_cleanup_") !=
             std::string::npos,
         "active caller cleanups lower to context-aware panic thunks");
  expect(ir.find("call void @janus_free(ptr") != std::string::npos,
         "delete releases closure environments");
  expect(ir.find("define { ptr, ptr } @makeIdentity__int()") !=
             std::string::npos,
         "generic factories specialize function values");

  expect_compile_error("def main() : int { val f : (int) => int = "
                       "(value : double) => int(value) delete f return 0 }",
                       "cannot use expression of type '(double) => int'");
  expect_compile_error(
      "def main() : int { val f : (int) => int = "
      "(value : int) => value val result : int = f() delete f return result }",
      "expects 1 argument");
  expect_compile_error("def main() : int { val f : (int) => int = "
                       "(value : int) => value delete f return f(1) }",
                       "used before initialization");
  expect_compile_error(
      "def main() : int { val f = (value : int) => { if value > 0 { "
      "return value } return true } delete f return 0 }",
      "lambda block returns have inconsistent types");
  expect_compile_error(
      "def main() : int { val f = (value : int) => { if value > 0 { "
      "return } return value } delete f return 0 }",
      "lambda block returns have inconsistent types");
  expect_compile_error(
      "def main() : int { val f = (value : int) => { if value > 0 { "
      "return value } } delete f return 0 }",
      "not all paths return a value");
  expect_compile_error(
      "class Token() {} def main() : int { val f = (value : int) => { "
      "val token = new Token() defer delete token return token } "
      "delete f return 0 }",
      "scheduled for deferred cleanup");
  expect_compile_error(
      "def main() : int { while true { val f = () => { break } "
      "delete f break } return 0 }",
      "break can only be used inside a loop");
  expect_compile_error(
      "def main() : int { while true { val f = () => { continue } "
      "delete f break } return 0 }",
      "continue can only be used inside a loop");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "val resource : Resource = new Resource() "
      "val cleanup = () => { delete resource } delete cleanup "
      "delete resource return 0 }",
      "cannot be deleted from a loop, branch expression, or closure");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "val resource : Resource = new Resource() "
      "val cleanup = () => { defer delete resource } delete cleanup "
      "delete resource return 0 }",
      "cannot be deleted from a loop, branch expression, or closure");
  expect_compile_error(
      "def main() : int { val pointer : Ptr[int] = alloc[int](usize(1)) "
      "val cleanup = () => { free(pointer) } delete cleanup "
      "free(pointer) return 0 }",
      "cannot be released from a loop, branch expression, or closure");
  expect_compile_error(
      "class Resource() { consume def finish() : Unit { delete this } } "
      "def main() : int { val resource : Resource = new Resource() "
      "val cleanup = () => { resource.finish() } delete cleanup "
      "delete resource return 0 }",
      "cannot be consumed from a loop, branch expression, or closure");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "val dispose = (value : Resource) => { delete value } "
      "val resource : Resource = new Resource() dispose(resource) "
      "delete resource delete dispose return 0 }",
      "cannot be deleted from a loop, branch expression, or closure");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "val dispose = (value : Resource) => { defer delete value } "
      "val resource : Resource = new Resource() dispose(resource) "
      "delete resource delete dispose return 0 }",
      "cannot be deleted from a loop, branch expression, or closure");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "val take = (value : Resource) => { val owned = move value delete owned } "
      "val resource : Resource = new Resource() take(resource) "
      "delete resource delete take return 0 }",
      "cannot be moved from a loop, branch expression, or closure");
  expect_compile_error(
      "class Resource() { consume def finish() : Unit { delete this } } "
      "def main() : int { val finish = (value : Resource) => { value.finish() } "
      "val resource : Resource = new Resource() finish(resource) "
      "delete resource delete finish return 0 }",
      "cannot be consumed from a loop, branch expression, or closure");
  expect_compile_error(
      "def main() : int { val release = (value : Ptr[int]) => { free(value) } "
      "val pointer : Ptr[int] = alloc[int](usize(1)) release(pointer) "
      "free(pointer) delete release return 0 }",
      "cannot be released from a loop, branch expression, or closure");
  expect_compile_error(
      "class Resource() {} def main() : int { "
      "val value : Resource = new Resource() "
      "val dispose = (value : Resource) => { delete value } "
      "dispose(value) delete value delete dispose return 0 }",
      "cannot be deleted from a loop, branch expression, or closure");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "lambdas are captured, generic first-class values\n";
  return 0;
}
