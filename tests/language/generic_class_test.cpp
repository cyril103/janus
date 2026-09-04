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

void expect_exact_compile_error(std::string_view source,
                                std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    expect(false, "invalid generic class source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()} == expected_message,
           "ambiguous generic constructor reports the complete diagnostic, "
           "including the [T] help");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Box[T](var value : T) {
    borrow def get() : borrow T {
        return value
    }
    def set(next : T) : T {
        value = move next
        return move value
    }
    destructor {
    }
}

class Empty[T]() {}

def main() : int {
    val argumentDriven = new Box(42)
    val contextDriven : Box[int] = new Box(42)
    val integers = new Box(41)
    val result : int = argumentDriven.get() + contextDriven.set(42)
    val text = new Box("Janus")
    text.get()
    val nested = new Box(move integers)
    nested.get()
    val empty : Empty[int] = new Empty()
    delete empty
    delete nested
    delete text
    delete contextDriven
    delete argumentDriven
    return result - 84
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
  const auto *integer_constructor =
      std::get_if<janus::ast::NewExpression>(&integer_box.initializer->value);
  expect(!integer_box.declared_type.has_value() &&
             integer_constructor != nullptr &&
             integer_constructor->type_arguments.empty(),
         "argument-driven constructor inference omits both annotations");
  const auto &context_box =
      std::get<janus::ast::ValueDeclaration>(program.functions[0].body[1]);
  const auto *context_constructor =
      std::get_if<janus::ast::NewExpression>(&context_box.initializer->value);
  expect(context_box.declared_type.has_value() &&
             context_constructor != nullptr &&
             context_constructor->type_arguments.empty(),
         "context-driven constructor inference omits constructor arguments");

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.functions.at("main").at("argumentDriven").type.name() ==
                 "Box[int]" &&
             analysis.functions.at("main").at("contextDriven").type.name() ==
                 "Box[int]",
         "argument- and context-driven inference retain concrete class arguments");
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
  expect_exact_compile_error(
      "class Factory[T]() {} "
      "def main() : int { val factory = new Factory() return 0 }",
      "cannot infer type of 'factory'; help: add an explicit type annotation; "
      "note: generic type parameter 'T' is not constrained by constructor "
      "arguments or context; help: add explicit type arguments");
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
