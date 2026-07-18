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
    expect(false, "invalid generic class source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "generic class error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Box[T](var value : T) {
    def get() : T {
        return value
    }
    def set(next : T) : T {
        value = next
        return value
    }
    destructor {
    }
}

def main() : int {
    val integers : Box[int] = new Box[int](41)
    val result : int = integers.set(42)
    val text : Box[string] = new Box[string]("Janus")
    val message : string = text.get()
    val nested : Box[Box[int]] = new Box[Box[int]](integers)
    val inner : Box[int] = nested.get()
    delete nested
    delete text
    delete integers
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.classes.front().type_parameters.size() == 1,
         "Box declares one type parameter");
  expect(program.classes.front().type_parameters.front() == "T",
         "the class type parameter is named T");
  const auto &integer_box =
      std::get<janus::ast::ValueDeclaration>(program.functions[0].body[0]);
  expect(integer_box.declared_type.name == "Box" &&
             integer_box.declared_type.type_arguments.size() == 1 &&
             integer_box.declared_type.type_arguments[0].name == "int",
         "Box[int] is represented as a parameterized type");

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.functions.at("main").at("integers").type.name() == "Box[int]",
         "semantic types retain concrete class arguments");
  expect(analysis.functions.at("Box.get").at("value").type.name() == "T",
         "a generic field remains symbolic while its method is analyzed");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "generic_classes");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("%class.Box__int = type { i32 }") != std::string::npos,
         "Box[int] has a specialized LLVM layout");
  expect(ir.find("%class.Box__string = type { { ptr, i64 } }") !=
             std::string::npos,
         "Box[string] has a distinct specialized LLVM layout");
  expect(ir.find("%class.Box__Box__int = type { ptr }") != std::string::npos,
         "nested class type arguments are monomorphized");
  expect(ir.find("define i32 @Box__int__get") != std::string::npos,
         "Box[int].get returns i32");
  expect(ir.find("define { ptr, i64 } @Box__string__get") != std::string::npos,
         "Box[string].get returns the string representation");
  expect(ir.find("call i32 @Box__int__set") != std::string::npos,
         "a specialized generic-class method can be called");
  expect(ir.find("call void @Box__int__destructor") != std::string::npos &&
             ir.find("call void @Box__string__destructor") != std::string::npos,
         "each specialization has its own destructor");

  expect_compile_error(
      "class Box[T](val value : T) {} "
      "def main() : int { val box : Box = new Box[int](1) return 0 }",
      "expects 1 type argument");
  expect_compile_error(
      "class Box[T](val value : T) {} "
      "def main() : int { val box : Box[int] = new Box(1) return 0 }",
      "expects 1 type argument");
  expect_compile_error(
      "class Box[T](val value : T) {} "
      "def main() : int { val box : Box[int] = new Box[string](\"x\") "
      "return 0 }",
      "where type 'Box[int]' is required");
  expect_compile_error(
      "class Box[T](val value : T) {} "
      "def main() : int { val box : Box[int] = new Box[int](true) return 0 }",
      "expression of type 'bool'");
  expect_compile_error("class Pair[T, T](val value : T) {} "
                       "def main() : int { return 0 }",
                       "type parameter 'T' is already declared");
  expect_compile_error("class Invalid[T](val value : int[T]) {} "
                       "def main() : int { return 0 }",
                       "built-in type 'int' does not accept type arguments");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "generic classes are type checked and monomorphized\n";
  return 0;
}
