#include "janus/ast/ast.hpp"
#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"
#include "janus/types/type.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <string>
#include <string_view>
#include <variant>

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
    expect(false, "invalid source must produce a compile error");
  } catch (const janus::CompileError &error) {
    if (std::string_view{error.what()}.find(expected_message) ==
        std::string_view::npos) {
      std::cerr << "FAILED: expected diagnostic containing '" << expected_message
                << "', got '" << error.what() << "'\n";
      ++failures;
    }
  }
}

} // namespace

int main() {
  janus::frontend::Parser inferred_parser{
      "def main() : int { val inferred = 5 return inferred }"};
  const janus::ast::Program inferred_program = inferred_parser.parse_program();
  const auto &inferred_declaration =
      std::get<janus::ast::ValueDeclaration>(
          inferred_program.functions.front().body.front());
  expect(!inferred_declaration.declared_type.has_value(),
         "an inferred local explicitly omits its AST type annotation");
  janus::semantic::Analyzer inferred_analyzer;
  const janus::semantic::AnalysisResult inferred_analysis =
      inferred_analyzer.analyze(inferred_program);
  expect(inferred_analysis.functions.at("main").at("inferred").type.name() ==
             "int",
         "the analyzer records the canonical inferred local type");
  expect(inferred_analysis.local_types.at(&inferred_declaration).name() ==
             "int",
         "analysis exposes the canonical type keyed by the local AST node");

  llvm::LLVMContext inferred_context;
  janus::backend::llvm::IrGenerator inferred_generator{inferred_context};
  const std::unique_ptr<llvm::Module> inferred_module =
      inferred_generator.generate(inferred_program, "inferred_local");
  std::string inferred_ir;
  llvm::raw_string_ostream inferred_output{inferred_ir};
  inferred_module->print(inferred_output, nullptr);
  inferred_output.flush();
  expect(inferred_ir.find("%inferred = alloca i32") != std::string::npos,
         "the backend consumes the analyzed inferred type");

  janus::frontend::Parser expression_parser{R"(
struct Point(val x : int) derives Copy {}
class Box(val value : int) { def get() : int { return value } }
class Resource(val value : int) {}
enum Choice { Number(int), Empty }
def identity[T](value : T) : T { return move value }
def makePoint() : Point { return new Point(7) }
def main() : int {
  val point = makePoint()
  val constructed = new Box(point.x)
  val field = constructed.value
  val method = constructed.get()
  val selected = if true { field } else { method }
  val choice = Choice.Number(selected)
  val matched = match choice { Number(value) => value, Empty => 0 }
  val operated = matched + 1
  val generic = identity(operated)
  val resource = new Resource(9)
  val moved = move resource
  borrow val pointer = null[int]()
  delete constructed
  delete moved
  return generic
}
)"};
  const janus::ast::Program expression_program = expression_parser.parse_program();
  const janus::semantic::AnalysisResult expression_analysis =
      inferred_analyzer.analyze(expression_program);
  const auto &expression_symbols = expression_analysis.functions.at("main");
  expect(expression_symbols.at("point").type.name() == "Point",
         "function calls infer their return type");
  expect(expression_symbols.at("constructed").type.name() == "Box",
         "constructors infer their class type");
  expect(expression_symbols.at("field").type.name() == "int" &&
             expression_symbols.at("method").type.name() == "int",
         "field and method accesses infer their result type");
  expect(expression_symbols.at("selected").type.name() == "int" &&
             expression_symbols.at("matched").type.name() == "int",
         "convergent if and exhaustive match expressions infer a type");
  expect(expression_symbols.at("operated").type.name() == "int" &&
             expression_symbols.at("generic").type.name() == "int",
         "operators and fully constrained generic calls infer a type");
  expect(expression_symbols.at("moved").type.name() == "Resource" &&
             expression_symbols.at("pointer").type.name() == "Ptr[int]",
         "move and borrow preserve the inferred semantic type");
  const std::unique_ptr<llvm::Module> expression_module =
      inferred_generator.generate(expression_program, "inferred_expressions");
  expect(expression_module != nullptr,
         "the backend emits all supported inferred expression types");

  janus::frontend::Parser parser{
      "def main() : int { val x : int = 5 return 0 }"};
  const janus::ast::Program program = parser.parse_program();

  expect(program.functions.size() == 1, "one function is parsed");
  expect(program.functions.front().name == "main",
         "the entry point is named main");
  expect(program.functions.front().return_type.name == "int",
         "main returns int");
  expect(program.functions.front().body.size() == 2,
         "main contains a declaration and a return");
  if (program.functions.front().body.size() == 2) {
    const janus::ast::ValueDeclaration &declaration =
        std::get<janus::ast::ValueDeclaration>(
            program.functions.front().body.front());
    expect(declaration.name == "x", "the identifier is x");
    expect(declaration.declared_type.has_value() &&
               declaration.declared_type->name == "int",
           "the declared type is int");
    expect(!declaration.is_mutable, "a val declaration is immutable");
    expect(std::get<janus::ast::IntegerLiteralExpression>(
               declaration.initializer->value)
                   .magnitude == 5,
           "the initializer is the integer literal 5");
  }

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  const janus::semantic::SymbolTable &symbols = analysis.functions.at("main");
  expect(symbols.contains("x"), "x is entered in the symbol table");
  expect(!symbols.at("x").is_mutable, "x is immutable");
  expect(symbols.at("x").type.concrete->is_signed(), "x has a signed type");
  expect(symbols.at("x").type.concrete->bit_width() == 32,
         "x has a 32-bit type");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module = generator.generate(program);
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("%x = alloca i32") != std::string::npos,
         "LLVM allocates i32 storage for x");
  expect(ir.find("store i32 5, ptr %x") != std::string::npos,
         "LLVM stores the evaluated value 5 in x");
  expect(ir.find("ret i32 0") != std::string::npos,
         "LLVM returns the value from main");
  expect(ir.find("define i32 @main(i32 %argc, ptr %argv)") !=
             std::string::npos,
         "the native entry point receives argc and argv");
  expect(ir.find("call void @janus_process_initialize(i32 %argc, ptr %argv)") !=
             std::string::npos,
         "the entry point initializes program arguments");

  janus::frontend::Parser builtin_parser{R"(
def main() : int {
  val ratio : double = 3.5
  val small : byte = 127
  val glyph : char = '😀'
  val ready : bool = true
  val stopped : bool = false
  val message : string = "Bonjour 😀\n"
  return 0
}
)"};
  const janus::ast::Program builtin_program = builtin_parser.parse_program();
  static_cast<void>(analyzer.analyze(builtin_program));
  const std::unique_ptr<llvm::Module> builtin_module =
      generator.generate(builtin_program, "builtin_literals");
  std::string builtin_ir;
  llvm::raw_string_ostream builtin_output{builtin_ir};
  builtin_module->print(builtin_output, nullptr);
  builtin_output.flush();

  expect(builtin_ir.find("%ratio = alloca double") != std::string::npos,
         "double storage uses LLVM double");
  expect(builtin_ir.find("%small = alloca i8") != std::string::npos,
         "byte storage uses LLVM i8");
  expect(builtin_ir.find("store i8 127") != std::string::npos,
         "a checked integer literal initializes byte");
  expect(builtin_ir.find("store i32 128512") != std::string::npos,
         "a Unicode scalar initializes char");
  expect(builtin_ir.find("store i1 true") != std::string::npos,
         "true initializes bool");
  expect(builtin_ir.find("store i1 false") != std::string::npos,
         "false initializes bool");
  expect(builtin_ir.find("%message = alloca { ptr, i64 }") != std::string::npos,
         "string storage contains a pointer and a length");
  expect(builtin_ir.find("i64 13") != std::string::npos,
         "string stores its UTF-8 byte length without the terminator");
  expect(builtin_ir.find("@.str.0") != std::string::npos,
         "string literal data is stored in a private global constant");

  expect_compile_error("def main() : int { val x int = 5 return 0 }",
                       "inferred local 'x' requires an initializer");
  expect_compile_error("val global = 5 def main() : int { return 0 }",
                       "global value 'global' requires an explicit type annotation");
  expect_compile_error(
      "class Bad() { val field = 1 } def main() : int { return 0 }",
      "field 'field' requires an explicit type annotation");
  expect_compile_error(
      "def main() : int { val value = null() return 0 }",
      "cannot infer type of 'value'");
  expect_compile_error(
      "class Collection[T]() {} def main() : int { "
      "val empty = new Collection() return 0 }",
      "cannot infer type of 'empty'");
  expect_compile_error(
      "def main() : int { val value = if true { 1 } else { false } return 0 }",
      "cannot infer type of 'value'");
  expect_compile_error("def main() : int { val x : unknown = 5 return 0 }",
                       "unknown type 'unknown'");
  expect_compile_error("def main() : int { val x : int = 2147483648 return 0 }",
                       "outside the signed 32-bit range");
  expect_compile_error(
      "def main() : int { val x : int = 1; val x : int = 2 return 0 }",
      "value 'x' is already declared");
  expect_compile_error("def main() : int { val x : int = 5; x = 6 return 0 }",
                       "cannot assign to immutable value 'x'");
  expect_compile_error("def helper() : int { return 0 }",
                       "must declare an entry point 'main'");
  expect_compile_error(
      "def main() : int { return 0 } def main() : int { return 0 }",
      "function 'main' is already declared");
  expect_compile_error("def main() : int { val x : int = 5 }",
                       "must return a value");
  expect_compile_error("def main() : int { return 0 val x : int = 5 }",
                       "unreachable statement");
  expect_compile_error("def main() : int { val value : byte = 128 return 0 }",
                       "outside the signed 8-bit range");
  expect_compile_error("def main() : int { val value : bool = 1 return 0 }",
                       "expression of type 'int'");
  expect_compile_error("def main() : int { val value : double = 1 return 0 }",
                       "expression of type 'int'");
  expect_compile_error("def main() : int { val value : char = 'ab' return 0 }",
                       "exactly one Unicode character");
  expect_compile_error("def main() : int { return false }",
                       "expression of type 'bool'");
  expect_compile_error(
      "def main() : int { val message : string = 'x' return 0 }",
      "expression of type 'char'");
  expect_compile_error(
      "def main() : int { val message : string = \"unterminated }",
      "unterminated string literal");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "val x : int = 5 -> typed declaration and LLVM store\n";
  return 0;
}
