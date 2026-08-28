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
    expect(false, "invalid mutable borrow program must be rejected");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           expected_message);
    expect(error.diagnostic().code != janus::DiagnosticCode::Unclassified,
           "mutable borrow errors use structured diagnostic codes");
  }
}

} // namespace

int main() {
  try {
    janus::frontend::Parser qualifier_parser{R"(
trait MutableAccess[T] {
  def value() : borrow var T
}
def accept[T](callback : () => borrow var T) : Unit {}
def main() : int { return 0 }
)"};
    const janus::ast::Program qualifiers = qualifier_parser.parse_program();
    expect(qualifiers.traits.front().methods.front().return_ownership ==
               janus::ast::ReturnOwnership::BorrowMutable,
           "trait methods parse mutable borrowed returns");
    expect(qualifiers.functions.front()
               .parameters.front()
               .type.function_return_ownership ==
               janus::ast::ReturnOwnership::BorrowMutable,
           "function types parse mutable borrowed returns");
  } catch (const std::exception &error) {
    std::cerr << "FAILED: mutable return qualifiers were rejected: "
              << error.what() << '\n';
    ++failures;
  }

  constexpr std::string_view valid_source = R"(
class Counter(var value : int) {
  borrow def read() : int { return value }
  def add(amount : int) : Unit { value = value + amount }
  def mutableValue() : borrow var int { return value }
}
struct Point(var x : int) {}
def increment(borrow var counter : Counter) : Unit { counter.add(1) }
def movePoint(borrow var point : Point) : Unit { point.x += 4 }
def increase(borrow var value : int) : Unit { value += 1 }
def identity(borrow var counter : Counter) : borrow var Counter {
  return counter
}
def main() : int {
  val counter : Counter = new Counter(1)
  if true {
    borrow var editable : Counter = counter
    editable.add(2)
  }
  increment(counter)
  val observed : int = counter.read()
  val point : Point = new Point(3)
  movePoint(point)
  var number : int = 5
  if true {
    borrow var editableNumber : int = number
    editableNumber = 7
  }
  increase(number)
  if true {
    borrow var returned : int = counter.mutableValue()
    returned = 9
  }
  if true {
    borrow var same : Counter = identity(counter)
    same.add(1)
  }
  delete counter
  return observed + point.x + number
}
)";
  try {
    janus::frontend::Parser parser{valid_source};
    const janus::ast::Program program = parser.parse_program();
    expect(program.functions[0].parameters[0].ownership ==
               janus::ast::ParameterOwnership::BorrowMutable,
           "borrow var parameters retain their mutable ownership effect");
    expect(program.functions[3].return_ownership ==
               janus::ast::ReturnOwnership::BorrowMutable,
           "borrow var return qualifiers retain their ownership effect");
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    llvm::LLVMContext context;
    janus::backend::llvm::IrGenerator generator{context};
    const auto module = generator.generate(program, "mutable_borrow");
    std::string ir;
    llvm::raw_string_ostream output{ir};
    module->print(output, nullptr);
    output.flush();
    expect(ir.find("define void @movePoint(ptr %point)") != std::string::npos,
           "a mutably borrowed struct is passed by address");
    expect(ir.find("store i32") != std::string::npos,
           "mutation through an exclusive borrow reaches shared storage");
  } catch (const std::exception &error) {
    std::cerr << "FAILED: valid mutable borrow source was rejected: "
              << error.what() << '\n';
    ++failures;
  }

  expect_compile_error(R"(
class Counter(var value : int) {}
def main() : int {
  val counter : Counter = new Counter(1)
  borrow val shared : Counter = counter
  borrow var editable : Counter = counter
  return shared.value
}
)", "already borrowed");

  expect_compile_error(R"(
class Counter(var value : int) {}
def main() : int {
  val counter : Counter = new Counter(1)
  borrow var editable : Counter = counter
  borrow val shared : Counter = counter
  return editable.value + shared.value
}
)", "cannot be accessed while mutably borrowed");

  expect_compile_error(R"(
class Counter(var value : int) {}
def main() : int {
  val counter : Counter = new Counter(1)
  borrow var first : Counter = counter
  borrow var second : Counter = counter
  return first.value + second.value
}
)", "cannot be accessed while mutably borrowed");

  expect_compile_error(R"(
class Counter(var value : int) {}
def main() : int {
  val counter : Counter = new Counter(1)
  borrow var editable : Counter = counter
  return counter.value + editable.value
}
)", "cannot be accessed while mutably borrowed");

  expect_compile_error(R"(
class Counter(var value : int) {}
def editBoth(borrow var left : Counter, borrow var right : Counter) : Unit {}
def main() : int {
  val counter : Counter = new Counter(1)
  editBoth(counter, counter)
  return 0
}
)", "more than once in the same call");

  expect_compile_error(R"(
class Counter(var value : int) {}
def main() : int {
  val counter : Counter = new Counter(1)
  borrow var editable : Counter = counter
  editable = counter
  return 0
}
)", "cannot reassign mutable borrow 'editable'");

  expect_compile_error(R"(
class Counter(var value : int) {}
def main() : int {
  borrow var editable : Counter = new Counter(1)
  return 0
}
)", "requires a local value identifier");

  expect_compile_error(R"(
class Counter(var value : int) {
  borrow def invalid() : borrow var int { return value }
}
def main() : int { return 0 }
)", "cannot be declared 'borrow def'");

  expect_compile_error(R"(
class Counter(var value : int) {}
def invalid(borrow counter : Counter) : borrow var Counter {
  return counter
}
def main() : int { return 0 }
)", "requires one 'borrow var' parameter");

  expect_compile_error(R"(
class Counter(var value : int) {
  borrow def shared() : borrow Counter { return this }
}
def main() : int {
  val counter : Counter = new Counter(1)
  borrow var invalid : Counter = counter.shared()
  return 0
}
)", "requires a 'borrow var' return");

  constexpr std::string_view borrowed_field_source = R"(
class SharedView(private borrow val source : Counter) {
  borrow def read() : int { return source.value }
}
class MutableView(private borrow var source : Counter) {
  def add(amount : int) : Unit { source.value = source.value + amount }
}
class Counter(var value : int) {}
def main() : int {
  val counter : Counter = new Counter(1)
  if true {
    val view : MutableView = new MutableView(counter)
    view.add(2)
    delete view
  }
  val shared : SharedView = new SharedView(counter)
  val result : int = shared.read()
  delete shared
  delete counter
  return result
}
)";
  try {
    janus::frontend::Parser parser{borrowed_field_source};
    const janus::ast::Program program = parser.parse_program();
    expect(program.classes[1].constructor_fields[0].is_borrowed &&
               program.classes[1].constructor_fields[0].is_mutable,
           "borrow var class fields retain exclusive borrowing semantics");
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
  } catch (const std::exception &error) {
    std::cerr << "FAILED: borrowed class field source was rejected: "
              << error.what() << '\n';
    ++failures;
  }

  expect_compile_error(R"(
class Counter(var value : int) {}
class MutableView(private borrow var source : Counter) {}
def main() : int {
  val counter : Counter = new Counter(1)
  val first : MutableView = new MutableView(counter)
  val second : MutableView = new MutableView(counter)
  return 0
}
)",
                       "already borrowed");

  expect_compile_error(R"(
class Counter(var value : int) {}
class MutableView(private borrow var source : Counter) {}
def main() : int {
  val counter : Counter = new Counter(1)
  borrow val shared : Counter = counter
  val view : MutableView = new MutableView(shared)
  return 0
}
)",
                       "cannot be borrowed mutably");

  expect_compile_error(R"(
class Counter(var value : int) {}
class SharedView(private borrow val source : Counter) {}
def main() : int {
  val counter : Counter = new Counter(1)
  borrow var editable : Counter = counter
  val view : SharedView = new SharedView(editable)
  return 0
}
)",
                       "cannot be borrowed concurrently");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "mutable borrows enforce exclusive visible mutation\n";
  return 0;
}
