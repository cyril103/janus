#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
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
    const bool matches = std::string_view{error.what()}.find(expected_message) !=
                         std::string_view::npos;
    if (!matches)
      std::cerr << "unexpected diagnostic: " << error.what() << '\n';
    expect(matches, "function value error contains the expected explanation");
  }
}

void expect_analyzes(std::string_view source, std::string_view message) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
    expect(true, message);
  } catch (const janus::CompileError &error) {
    std::cerr << "unexpected compile error: " << error.what() << '\n';
    expect(false, message);
  }
}

} // namespace

int main() {
  {
    janus::frontend::Parser warning_parser{R"(
class Resource(val marker : int) {}
def makeReader() : (int) => int {
    val resource : Resource = new Resource(41)
    return value => value + resource.marker
}
def main() : int { return 0 }
)"};
    janus::semantic::Analyzer warning_analyzer;
    const janus::semantic::AnalysisResult warning_analysis =
        warning_analyzer.analyze(warning_parser.parse_program());
    const std::size_t escaping_capture_warnings = std::count_if(
        warning_analysis.diagnostics.begin(), warning_analysis.diagnostics.end(),
        [](const janus::Diagnostic &diagnostic) {
          return diagnostic.code ==
                 janus::DiagnosticCode::AnalyzerEscapingOwningCapture;
        });
    expect(escaping_capture_warnings == 1,
           "contextual inference emits an existing warning exactly once");
  }

  expect_analyzes(
      R"(
enum Choice { First, Second }
class Box[T](val value : T) {}
class Calculator() {
    def combine(callback : (int, int) => int) : int {
        return callback(20, 22)
    }
    def apply[T](value : T, callback : (T) => int) : int {
        return callback(value)
    }
    def mixed[T](callback : (T, T) => int) : int {
        return 0
    }
}
def apply[T, U](value : T, callback : (T) => U) : U {
    return callback(value)
}
def mixed[T](callback : (T, T) => int) : int {
    return 0
}
def chooseFactory() : (Choice) => int {
    return choice => match choice { First => 1, Second => 2 }
}
def main() : int {
    val increment : (int) => int = value => value + 1
    val add : (int, int) => int = (left, right) => left + right
    val constant : () => int = () => 7
    val boxed : Box[int] = new Box[int](40)
    val unbox : (Box[int]) => int = value => value.value
    val fromFunction : int = apply(1, value => value + 1)
    val calculator : Calculator = new Calculator()
    val fromMethod : int = calculator.combine((left, right) => left + right)
    val fromGenericMethod : int = calculator.apply(41, value => value + 1)
    val fromMixedFunction : int = mixed((left : int, right) => left + right)
    val fromMixedMethod : int =
        calculator.mixed((left : int, right) => left + right)
    val chooser = chooseFactory()
    val result : int = increment(unbox(boxed)) + add(fromFunction, fromMethod) +
        constant() + chooser(Choice.First()) + fromGenericMethod +
        fromMixedFunction + fromMixedMethod - 126
    delete chooser
    delete calculator
    delete unbox
    delete boxed
    delete constant
    delete add
    delete increment
    return result
}
)",
      "context supplies bare lambda parameter types before their bodies");

  expect_analyzes(
      R"(
def main() : int {
    val invoke : ((int) => int) => int =
        (callback : (int) => int) => callback(41)
    val result : int = invoke(value => value + 1)
    delete invoke
    return result - 42
}
)",
      "contextual lambdas preserve explicitly typed function parameters");

  expect_analyzes(
      R"(
class Counter(var value : int) {}
def inspect(callback : (borrow Counter) => int, borrow counter : Counter) : int {
    return callback(counter)
}
def edit(callback : (borrow var Counter) => Unit, borrow var counter : Counter) : Unit {
    callback(counter)
}
def main() : int {
    val counter : Counter = new Counter(41)
    val observed : int = inspect((borrow value) => value.value, counter)
    edit((borrow var value) => { value.value = value.value + 1 }, counter)
    delete counter
    return observed
}
)",
      "borrow and borrow var remain explicit while their types are contextual");

  expect_compile_error(
      "def main() : int { val identity = value => value return 0 }",
      "annotat");
  expect_compile_error(
      "def main() : int { val callback : (int, int) => int = value => value "
      "delete callback return 0 }",
      "expects 2 parameter");
  expect_compile_error(
      "class Box() {} def main() : int { val callback : (Box) => int = "
      "(borrow value) => 1 delete callback return 0 }",
      "ownership");

  expect_analyzes(
      R"(
class Box() {}
def consumeUnit(value : Box) : Unit { delete value }
def main() : int {
    val consumeOnce = () => {
        val x : Box = new Box()
        return consumeUnit(move x)
    }
    delete consumeOnce
    return 0
}
)",
      "a Unit lambda return evaluates an owning move exactly once");

  expect_compile_error(
      R"(
class Iterator[T]() {}
def main() : int {
    val iterator : Iterator[int] = new Iterator[int]()
    val iterConsumer = () => {
        for item in iterator {}
    }
    delete iterConsumer
    delete iterator
    return 0
}
)",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");
  expect_compile_error(
      R"(
class Iterator[T]() {}
def main() : int {
    val iterConsumer = (iterator : Iterator[int]) => {
        for item in iterator {}
    }
    delete iterConsumer
    return 0
}
)",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");
  expect_analyzes(
      R"(
class Iterator[T]() {}
def main() : int {
    val iterateLocal = () => {
        val iterator : Iterator[int] = new Iterator[int]()
        for item in iterator {}
    }
    delete iterateLocal
    return 0
}
)",
      "a lambda may consume an Iterator created in its own body");
  expect_analyzes(
      R"(
class Resource() {}
class Iterator[T]() {}
def main() : int {
    val iterate = (item : Resource) => {
        val iterator : Iterator[Resource] = new Iterator[Resource]()
        for item in iterator { delete item }
    }
    delete iterate
    return 0
}
)",
      "a for binding may shadow a protected lambda parameter");
  expect_analyzes(
      R"(
class Resource() {}
enum Slot { Some(Resource), None }
def dispose(value : Resource) : int { delete value return 1 }
def main() : int {
    val inspect = (item : Resource) => {
        val slot : Slot = Slot.Some(new Resource())
        return match move slot {
            Some(item) => dispose(move item),
            None => 0
        }
    }
    delete inspect
    return 0
}
)",
      "a match binding may shadow a protected lambda parameter");

  expect_compile_error(
      R"(
class Resource() {}
class Owner(val ownedField : Resource) {}
def main() : int {
    val capture = new Owner(new Resource())
    val cleanup = () => { delete capture.ownedField }
    delete cleanup
    delete capture
    return 0
}
)",
      "cannot be deleted from a loop, branch expression, or closure");
  expect_compile_error(
      R"(
class Resource() {}
class Owner(val ownedField : Resource) {}
def main() : int {
    val cleanup = (owner : Owner) => { delete owner.ownedField }
    delete cleanup
    return 0
}
)",
      "cannot be deleted from a loop, branch expression, or closure");
  expect_compile_error(
      R"(
class BufferOwner(val pointer : Ptr[int]) {}
def main() : int {
    val holder = new BufferOwner(alloc[int](usize(1)))
    val cleanup = () => { defer free(holder.pointer) }
    delete cleanup
    delete holder
    return 0
}
)",
      "cannot be released from a loop, branch expression, or closure");
  expect_compile_error(
      R"(
class BufferOwner(val pointer : Ptr[int]) {}
def main() : int {
    val cleanup = (holder : BufferOwner) => { freeStorage(holder.pointer) }
    delete cleanup
    return 0
}
)",
      "cannot be released from a loop, branch expression, or closure");
  expect_compile_error(
      R"(
class Iterator[T]() {}
class Owner(val iterator : Iterator[int]) {}
def main() : int {
    val capture = new Owner(new Iterator[int]())
    val iterate = () => { for item in capture.iterator {} }
    delete iterate
    delete capture
    return 0
}
)",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");
  expect_compile_error(
      R"(
class Iterator[T]() {}
class Owner(val iterator : Iterator[int]) {}
def main() : int {
    val iterate = (owner : Owner) => { for item in owner.iterator {} }
    delete iterate
    return 0
}
)",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");

  expect_compile_error(
      R"(
class Resource(val marker : int) {
    def dispose() : Unit { delete this }
}
def main() : int {
    val owner : Resource = new Resource(7)
    val factory = () => {
        return owningCapture[Resource](owner, () => owner.dispose())
    }
    delete factory
    delete owner
    return 0
}
)",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");
  expect_compile_error(
      R"(
class Resource(val marker : int) {
    def dispose() : Unit { delete this }
}
def main() : int {
    val factory = (owner : Resource) => {
        return owningCapture[Resource](owner, () => owner.dispose())
    }
    delete factory
    return 0
}
)",
      "cannot be consumed/transferred from a loop, branch expression, or "
      "closure");
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

class Resource(val marker : int) {
    def dispose() : Unit { delete this }
}

def makeLocalCleanup() : () => () => Unit {
    return () => {
        val local : Resource = new Resource(7)
        return owningCapture[Resource](local, () => local.dispose())
    }
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

    val factory = makeLocalCleanup()
    val cleanup = factory()
    cleanup()
    delete cleanup
    delete factory
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
  std::string verifier_error;
  llvm::raw_string_ostream verifier_output{verifier_error};
  expect(!llvm::verifyModule(*module, &verifier_output),
         "lambda IR does not reference instructions from its enclosing "
         "function: " + verifier_error);
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
