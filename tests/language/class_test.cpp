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
    destructor {
    }
}

def main() : int {
    val point : Point = new Point(1, 2)
    point.x = 6
    val result : int = point.x
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
  expect(program.classes.front().fields.size() == 1,
         "the class body can declare fields");
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

  expect(ir.find("%class.Point = type { i32, i32, { ptr, i64 } }") !=
             std::string::npos,
         "Point has an LLVM heap layout");
  expect(ir.find("call ptr @malloc") != std::string::npos,
         "new allocates Point with malloc");
  expect(ir.find("store i32 1") != std::string::npos,
         "the constructor initializes x");
  expect(ir.find("store i32 6") != std::string::npos,
         "a mutable field can be reassigned");
  expect(ir.find("call void @Point__destructor") != std::string::npos,
         "delete invokes the destructor");
  expect(ir.find("call void @free") != std::string::npos,
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
      "class Resource() { destructor { var state : int = 0 } } "
      "def main() : int { return 0 }",
      "non-empty destructor bodies are not yet supported");
  expect_compile_error(
      "class Point(var x : int) { def current() : int { return x } } "
      "def main() : int { val p : Point = new Point(1) return p.current() }",
      "method calls are not yet supported");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "class instances use manual heap allocation and deletion\n";
  return 0;
}
