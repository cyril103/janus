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
    expect(false, "invalid class source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "class error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Point(var x : int, var y : int) {
    val label : string = "point"
    private val secret : int = 42
    internal val moduleCode : int = 7
    private def secretValue() : int {
        return secret
    }
    def reveal() : int {
        return this.secretValue()
    }
    internal def moduleValue() : int {
        return moduleCode
    }
    def setX(value : int) : int {
        x = value
        return x
    }
    def currentX() : int {
        return this.x
    }
    destructor {
    }
}

def main() : int {
    val point : Point = new Point(1, 2)
    val changed : int = point.setX(6)
    val hidden : int = point.reveal()
    val moduleValue : int = point.moduleValue() + point.moduleCode
    val result : int = point.currentX()
    delete point
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.classes.size() == 1, "one class is parsed");
  expect(program.classes.front().name == "Point", "the class is Point");
  expect(program.classes.front().constructor_fields.size() == 2,
         "constructor parameters become fields");
  expect(program.classes.front().constructor_fields.front().is_mutable,
         "var constructor fields are mutable");
  expect(program.classes.front().fields.size() == 3,
         "the class body can declare fields");
  expect(program.classes.front().fields[1].is_private,
         "private marks a field as private");
  expect(program.classes.front().fields.back().is_internal,
         "internal marks a field as module-visible");
  expect(program.classes.front().methods.size() == 5,
         "the class body can declare methods");
  expect(program.classes.front().methods.front().is_private,
         "private marks a method as private");
  expect(program.classes.front().methods[2].is_internal,
         "internal marks a method as module-visible");
  expect(program.classes.front().destructor.has_value(),
         "the destructor is parsed");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "classes");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find(
             "%class.Point = type { i32, i32, { ptr, i64 }, i32, i32 }") !=
             std::string::npos,
         "Point has an LLVM heap layout");
  expect(ir.find("call ptr @janus_alloc") != std::string::npos,
         "new allocates Point with malloc");
  expect(ir.find("store i32 1") != std::string::npos,
         "the constructor initializes x");
  expect(ir.find("call i32 @Point__setX(ptr %point.object, i32 6)") !=
             std::string::npos,
         "a method receives the value used to mutate a field");
  expect(ir.find("define i32 @Point__setX(ptr %this, i32 %value)") !=
             std::string::npos,
         "a method receives an implicit this pointer");
  expect(ir.find("call i32 @Point__setX") != std::string::npos,
         "an instance method can be called");
  expect(ir.find("call i32 @Point__currentX") != std::string::npos,
         "a method can read a member through this");
  expect(ir.find("call i32 @Point__secretValue") != std::string::npos,
         "a class method can call a private method internally");
  expect(ir.find("define internal i32 @Point__moduleValue") !=
                 std::string::npos &&
             ir.find("call i32 @Point__moduleValue") != std::string::npos,
         "an internal method uses hidden linkage and remains callable locally");
  expect(ir.find("call void @Point__destructor") != std::string::npos,
         "delete invokes the destructor");
  expect(ir.find("call void @janus_free") != std::string::npos,
         "delete releases the allocation");

  expect_compile_error(
      "class Point(var x : int) {} "
      "def main() : int { val p : Point = new Point() return 0 }",
      "expects 1 argument");
  expect_compile_error(
      "class Point(var x : int) {} "
      "def main() : int { val p : Point = new Point(true) return 0 }",
      "expression of type 'bool'");
  expect_compile_error(
      "class Point(val x : int) {} "
      "def main() : int { val p : Point = new Point(1) p.x = 2 return 0 }",
      "cannot assign to immutable field 'x'");
  expect_compile_error(
      "class Point(var x : int) {} "
      "def main() : int { val p : Point = new Point(1) delete p return p.x }",
      "used before initialization");
  expect_compile_error("def main() : int { delete 1 return 0 }",
                       "delete requires an object");
  expect_compile_error(
      "class Point(var x : int) { def set(value : int) : int { x = value "
      "return x } } def main() : int { val p : Point = new Point(1) "
      "return p.set(true) }",
      "expression of type 'bool'");
  expect_compile_error(
      "class Point(val x : int) { def change(value : int) : int { x = value "
      "return x } } def main() : int { return 0 }",
      "cannot assign to immutable value 'x'");
  expect_compile_error("class Vault() { private val secret : int = 42 } "
                       "def main() : int { val vault : Vault = new Vault() "
                       "return vault.secret }",
                       "field 'secret' is private");
  expect_compile_error("class Vault() { private var secret : int = 42 } "
                       "def main() : int { val vault : Vault = new Vault() "
                       "vault.secret = 1 return 0 }",
                       "field 'secret' is private");
  expect_compile_error(
      "class Vault() { private def secret() : int { return 42 } } "
      "def main() : int { val vault : Vault = new Vault() "
      "return vault.secret() }",
      "method 'secret' is private");
  expect_compile_error(
      "class Secret(private val value : int) {} "
      "def main() : int { val secret : Secret = new Secret(42) "
      "return secret.value }",
      "field 'value' is private");
  expect_compile_error(
      "internal def hidden() : int { return 1 } "
      "def main() : int { return 0 }",
      "'internal' can only modify class fields and methods");
  expect_compile_error(
      "class Invalid() { private internal def hidden() : int { return 1 } } "
      "def main() : int { return 0 }",
      "cannot be both private and internal");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "class instances use manual heap allocation and deletion\n";
  return 0;
}
