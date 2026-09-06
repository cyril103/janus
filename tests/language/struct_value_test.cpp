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
    expect(false, "invalid struct source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "struct error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
struct Point(var x : int, var y : int) {
    def translate(dx : int, dy : int) : Unit {
        x = x + dx
        y = y + dy
    }

    borrow def sum() : int {
        return this.x + this.y
    }
}

def copyPoint(point : Point) : Point {
    return point
}

def main() : int {
    val original : Point = new Point(2, 3)
    var copied : Point = copyPoint(original)
    copied.translate(4, 5)
    return original.sum() + copied.sum()
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.classes.size() == 1, "one struct is parsed");
  expect(program.classes.front().is_value_type, "Point is a value type");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "struct_values");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("%struct.Point = type { i32, i32 }") != std::string::npos,
         "Point has an inline LLVM value layout");
  expect(ir.find("define %struct.Point @copyPoint(%struct.Point %point)") !=
             std::string::npos,
         "struct parameters and returns are passed by value");
  expect(ir.find("define i32 @Point__sum(ptr %this)") != std::string::npos,
         "native struct methods receive an address to the inline value");
  expect(ir.find("%this.addr = alloca ptr") == std::string::npos,
         "native struct methods do not add a second level of indirection");
  expect(ir.find("ptr %this, i32 0, i32 0") != std::string::npos,
         "explicit this.field access projects from the ABI receiver");
  expect(ir.find("call ptr @janus_alloc") == std::string::npos,
         "constructing a struct does not allocate");

  expect_compile_error(
      "struct Value(val x : int) {} def main() : int { val value : Value = "
      "new Value(1) delete value return 0 }",
      "struct values do not require delete");
  expect_compile_error(
      "struct Invalid(value : int, val x : int) {}",
      "struct constructors only support val/var fields");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "struct values are copied inline without allocation\n";
  return 0;
}
