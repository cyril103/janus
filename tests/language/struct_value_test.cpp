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

  constexpr std::string_view named_source = R"(
struct Pair[A, B](val first : A, val second : B) {}
struct Empty() {}
struct Accent(val café : int) {}
struct Point(val x : int, val y : int) {}
const origin : Point = new Point { y: 2, x: 1 }
def main() : int {
    val x = 10
    val y = 20
    val point = new Point { y, x, }
    val pair = new Pair { second: true, first: point.x }
    val explicit = new Pair[int, bool] { second: false, first: 3 }
    val empty = new Empty {}
    val café = 7
    val accented = new Accent { café }
    val nested = new Point { y: match x { 10 => y, _ => 0 }, x: if true { 1 } else { 2 } }
    return pair.first + explicit.first
}
)";
  try {
    janus::frontend::Parser named_parser{named_source};
    const auto named_program = named_parser.parse_program();
    const auto &main = named_program.functions.front();
    const auto &binding = std::get<janus::ast::ValueDeclaration>(main.body[2]);
    const auto &construction =
        std::get<janus::ast::NewExpression>(binding.initializer->value);
    expect(construction.is_named && construction.named_fields.size() == 2,
           "named syntax survives in the AST");
    expect(construction.named_fields[0].name == "y" &&
               construction.named_fields[0].shorthand,
           "shorthand preserves source order");
    expect(construction.named_fields[0].location.offset ==
               named_source.find("y, x"),
           "field source location points at the label");
    const auto analysis = analyzer.analyze(named_program);
    const auto &origin =
        std::get<std::shared_ptr<janus::constant::AggregateValue>>(
            analysis.global_constant_values.at("origin").data);
    expect(origin->fields.size() == 2 &&
               std::get<std::uint64_t>(origin->fields[0].second.data) == 1 &&
               std::get<std::uint64_t>(origin->fields[1].second.data) == 2,
           "constant named fields follow declaration storage order");
    static_cast<void>(generator.generate(named_program, "named_structs"));
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    expect(false, "named structs support generics and constants");
  }

  const auto named_error = [&](std::string_view expression,
                               janus::DiagnosticCode code) {
    try {
      const std::string invalid =
          "struct Point(val x : int, val y : int) {} "
          "struct Secret(private val x : int) {} class Object() {} "
          "def main() : int { val p = " +
          std::string{expression} + " return 0 }";
      janus::frontend::Parser invalid_parser{invalid};
      const auto invalid_program = invalid_parser.parse_program();
      static_cast<void>(analyzer.analyze(invalid_program));
      expect(false, "invalid named construction is rejected");
    } catch (const janus::CompileError &error) {
      expect(error.diagnostic().code == code, error.what());
    }
  };
  named_error("new Point { x: 1, z: 2 }",
              janus::DiagnosticCode::AnalyzerUnknownStructField);
  named_error("new Point { x: 1, x: 2 }",
              janus::DiagnosticCode::AnalyzerDuplicateStructField);
  named_error("new Point { x: 1 }",
              janus::DiagnosticCode::AnalyzerMissingStructField);
  named_error("new Secret { x: 1 }",
              janus::DiagnosticCode::AnalyzerInaccessibleStructField);
  named_error("new Object {}",
              janus::DiagnosticCode::AnalyzerNamedClassConstruction);
  expect_compile_error("struct Point(val x : int) {} def main() : int { val p "
                       "= new Point { x } return 0 }",
                       "unknown value 'x'");
  expect_compile_error("struct Point(val x : int) {} def main() : int { val p "
                       "= new Point { x: true } return 0 }",
                       "where type 'int' is required");

  expect_compile_error("struct Secret(private val x : int) {} const value : "
                       "Secret = new Secret { x: 1 } "
                       "def main() : int { return 0 }",
                       "is inaccessible");
  expect_compile_error(
      "class Resource() {} struct Owned(val a : Resource, val b : int) {} "
      "def main() : int { val r = new Resource() val p = new Owned { b: 1, a: "
      "r } return 0 }",
      "requires an explicit move");
  expect_compile_error(
      "class Resource() {} struct Owned(val a : Resource, val b : Resource) {} "
      "def main() : int { val r = new Resource() val p = new Owned { b: move "
      "r, a: move r } return 0 }",
      "used before initialization");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "struct values are copied inline without allocation\n";
  return 0;
}
