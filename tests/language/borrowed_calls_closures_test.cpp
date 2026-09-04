#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

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

std::string generate_ir(std::string_view source) {
  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "closure_allocation");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  return ir;
}

void expect_compile_error(std::string_view source,
                          std::string_view expected_message,
                          janus::DiagnosticCode expected_code =
                              janus::DiagnosticCode::AnalyzerLegacy) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
    expect(false, expected_message);
  } catch (const janus::CompileError &error) {
    if (std::string_view{error.what()}.find(expected_message) ==
        std::string_view::npos) {
      std::cerr << "FAILED: expected " << expected_message << ", got "
                << error.what() << '\n';
      ++failures;
    }
    expect(error.diagnostic().code != janus::DiagnosticCode::AnalyzerLegacy,
           "borrowed call errors use structured diagnostic codes");
    if (expected_code != janus::DiagnosticCode::AnalyzerLegacy)
      expect(error.diagnostic().code == expected_code,
             "borrowed call error uses the expected diagnostic code");
  }
}

} // namespace

int main() {
  expect_compile_error(R"(
class Box(var value : int) {
  borrow def read() : int { return value }
}
class Selector(val marker : int) {
  borrow def select(borrow other : Box) : borrow Box { return other }
}
def main() : int {
  val selector : Selector = new Selector(0)
  defer delete selector
  val victimShared : Box = new Box(123)
  borrow val dangling : Box = selector.select(victimShared)
  delete victimShared
  return dangling.read()
}
)",
                       "owning value 'victimShared' cannot be released while "
                       "borrowed by 'dangling'");

  expect_valid(R"(
class Box(var value : int) {
  borrow def read() : int { return value }
}
class Selector(val marker : int) {
  borrow def select(borrow other : Box) : borrow Box { return other }
}
def main() : int {
  val victim : Box = new Box(123)
  var result : int = 0
  if true {
    val selector : Selector = new Selector(0)
    borrow val view : Box = selector.select(victim)
    delete selector
    result = view.read()
  }
  delete victim
  return result
}
)");

  expect_compile_error(R"(
class Box(var value : int) {}
class Selector(val marker : int) {
  def select(borrow var other : Box) : borrow var Box { return other }
}
def main() : int {
  val selector : Selector = new Selector(0)
  defer delete selector
  val victimMutable : Box = new Box(123)
  borrow var dangling : Box = selector.select(victimMutable)
  delete victimMutable
  return 0
}
)",
                       "owning value 'victimMutable' cannot be released while "
                       "borrowed by 'dangling'");

  expect_compile_error(R"(
class Box(var value : int) {}
class Selector(val marker : int) {
  borrow def select(flag : bool, borrow other : Box) : borrow Box {
    if flag { return this }
    return other
  }
}
def main() : int { return 0 }
)",
                       "borrowed return has incompatible provenance sources");

  expect_compile_error(R"(
class Box(var value : int) {}
class Selector(val marker : int) {
  borrow def select[T](borrow other : T) : borrow T { return other }
  borrow def relay(borrow other : Box) : borrow Box {
    return this.select[Box](other)
  }
}
def main() : int {
  val selector : Selector = new Selector(0)
  defer delete selector
  val victimRelay : Box = new Box(123)
  borrow val dangling : Box = selector.relay(victimRelay)
  delete victimRelay
  return 0
}
)",
                       "owning value 'victimRelay' cannot be released while "
                       "borrowed by 'dangling'");

  expect_compile_error(R"(
class Box(var value : int) {}
class Selector(val marker : int) {
  borrow def valueOf(borrow other : Box) : borrow int {
    return other.value
  }
}
def main() : int {
  val selector : Selector = new Selector(0)
  defer delete selector
  val victimProjection : Box = new Box(123)
  borrow val dangling : int = selector.valueOf(victimProjection)
  delete victimProjection
  return dangling
}
)",
                       "owning value 'victimProjection' cannot be released while "
                       "borrowed by 'dangling'");

  expect_compile_error(R"(
class Box(var value : int) {}
class Selector(val marker : int) {
  borrow def selfOrIgnore(borrow other : Box) : borrow Selector { return this }
}
def main() : int {
  val selector : Selector = new Selector(0)
  val other : Box = new Box(123)
  defer delete other
  borrow val view : Selector = selector.selfOrIgnore(other)
  delete selector
  return view.marker
}
)",
                       "owning value 'selector' cannot be released while "
                       "borrowed by 'view'");

  expect_valid(R"(
class Box(val value : int) {
  borrow def identity() : borrow Box { return this }
  borrow def read() : int { return value }
}
def forward(borrow box : Box) : borrow Box { return box.identity() }
def main() : int {
  val box : Box = new Box(4)
  var result : int = 0
  if true {
    borrow val view : Box = forward(box)
    result = view.read()
  }
  delete box
  return result
}
)",
               true);

  expect_compile_error(R"(
class Box(val value : int) {}
def forward(borrow box : Box) : borrow Box { return box }
def main() : int {
  val box : Box = new Box(4)
  val escaped : Box = forward(box)
  delete escaped
  delete box
  return 0
}
)",
                       "borrowed call result must be bound with 'borrow val'");

  expect_compile_error(R"(
class Box(val value : int) {}
def invalid(borrow box : Box) : borrow Box {
  val local : Box = new Box(2)
  return local
}
def main() : int { return 0 }
)",
                       "borrowed return must originate from 'this' or from the "
                       "function's borrowed parameter");

  expect_compile_error(R"(
class Box(val value : int) {}
def main() : int {
  val owning : (Box) => int = (box : Box) => box.value
  val observer : (borrow Box) => int = owning
  delete observer
  return 0
}
)",
                       "cannot use expression of type");

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

  expect_valid(R"(
extern def nativeView(handle : isize) : borrow Ptr[byte]
class NativeOwner(private val handle : isize) {
  borrow def data() : borrow Ptr[byte] { return nativeView(handle) }
}
def main() : int { return 0 }
)");

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

  expect_valid(R"(
class Counter(val value : int) {
  borrow def read() : int { return value }
}
def invoke(scoped callback : () => int) : int {
  defer delete callback
  return callback()
}
def test(borrow counter : Counter) : int {
  return invoke(() => counter.read())
}
def main() : int { return 0 }
)",
               true);

  const std::string captureless_ir = generate_ir(R"(
def main() : int {
  val identity : (int) => int = (value : int) => value
  val result : int = identity(4)
  delete identity
  return result
}
)");
  expect(captureless_ir.find("call ptr @janus_alloc") == std::string::npos,
         "captureless closures use a null environment without allocation");

  const std::string scoped_ir = generate_ir(R"(
def invoke(scoped callback : () => int) : int {
  defer delete callback
  return callback()
}
def test(value : int) : int {
  return invoke(() => value + 1)
}
def main() : int { return test(4) }
)");
  expect(scoped_ir.find("lambda.environment.scoped = alloca") !=
             std::string::npos,
         "capturing closures passed directly to scoped parameters use stack "
         "environments");
  expect(scoped_ir.find("aggregate.lambda.owns.environment") !=
             std::string::npos,
         "closure cleanup consults the environment ownership bit");

  expect_compile_error(R"(
def leak(scoped action : () => int) : () => int {
  return action
}
def main() : int { return 0 }
)",
                       "cannot escape into a return value",
                       janus::DiagnosticCode::AnalyzerBorrowEscape);

  expect_compile_error(R"(
def leak(scoped action : () => int) : () => int {
  val alias : () => int = move action
  return move alias
}
def main() : int { return 0 }
)",
                       "scoped value 'alias'",
                       janus::DiagnosticCode::AnalyzerBorrowEscape);

  expect_compile_error(R"(
def leak(scoped action : () => int) : () => int {
  return if true { action } else { action }
}
def main() : int { return 0 }
)",
                       "cannot escape into a return value",
                       janus::DiagnosticCode::AnalyzerBorrowEscape);

  expect_compile_error(R"(
class Holder(var callback : () => int) {}
def store(scoped action : () => int) : Holder {
  return new Holder(move action)
}
def main() : int { return 0 }
)",
                       "cannot escape into an object of type 'Holder'",
                       janus::DiagnosticCode::AnalyzerBorrowEscape);

  expect_compile_error(R"(
class Holder(var callback : () => int) {}
def store(scoped action : () => int) : int {
  val holder : Holder = new Holder(() => 0)
  holder.callback = () => action()
  delete holder
  delete action
  return 0
}
def main() : int { return 0 }
)",
                       "closure captures borrowed value 'action'",
                       janus::DiagnosticCode::AnalyzerBorrowEscape);

  expect_compile_error(R"(
enum CallbackBox { Some(() => int), None }
def store(scoped action : () => int) : CallbackBox {
  return CallbackBox.Some(() => action())
}
def main() : int { return 0 }
)",
                       "closure captures borrowed value 'action'",
                       janus::DiagnosticCode::AnalyzerBorrowEscape);

  expect_compile_error(R"(
def retain(action : () => int) : int {
  defer delete action
  return action()
}
def forward(scoped action : () => int) : int {
  return retain(move action)
}
def main() : int { return 0 }
)",
                       "non-scoped parameter 'action'",
                       janus::DiagnosticCode::AnalyzerBorrowEscape);

  expect_compile_error(R"(
def leak(scoped action : () => int) : () => int {
  return () => action()
}
def main() : int { return 0 }
)",
                       "closure captures borrowed value 'action'",
                       janus::DiagnosticCode::AnalyzerBorrowEscape);

  expect_valid(R"(
def invoke[T](scoped action : () => T) : T {
  defer delete action
  return action()
}
def forward[T](scoped action : () => T) : T {
  if false {
    delete action
    panic("unreachable")
  }
  return invoke[T](move action)
}
def main() : int { return forward[int](() => 42) }
)",
               true);

  const std::string escaping_ir = generate_ir(R"(
def make(value : int) : () => int { return () => value }
def main() : int {
  val callback : () => int = make(4)
  val result : int = callback()
  delete callback
  return result
}
)");
  expect(escaping_ir.find("call ptr @janus_alloc") != std::string::npos,
         "escaping capturing closures retain heap-owned environments");

  expect_compile_error(R"(
def invalid(scoped value : int) : int { return value }
def main() : int { return 0 }
)",
                       "scoped parameters require a function type");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "borrowed calls and closure captures remain bounded\n";
  return 0;
}
