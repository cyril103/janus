#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
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

void expect_compile_error(std::string_view source,
                          std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    expect(false, "invalid trait declaration must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "trait error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Iterator[T]() {}

trait Iterable[T] {
    def iterator() : Iterator[T]
    def transform[U](value : T, function : (T) => U) : U
    consume def finish() : T
}

class Sequence[T](val value : T) extends Iterable[T] {
    def iterator() : Iterator[T] {
        return new Iterator[T]()
    }
    def transform[U](item : T, function : (T) => U) : U {
        return function(item)
    }
    consume def finish() : T {
        return value
    }
}

def visit[C <: Iterable[int]](sequence : C) : int {
    val iterator : Iterator[int] = sequence.iterator()
    delete iterator
    return 1
}

def main() : int {
    return visit[Sequence[int]](new Sequence[int](5))
}
)";
  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.traits.size() == 1, "one trait is parsed");
  expect(program.traits.front().name == "Iterable",
         "the trait retains its name");
  expect(program.traits.front().type_parameters.size() == 1,
         "generic trait parameters are parsed");
  expect(program.traits.front().methods.size() == 3,
         "trait method signatures are parsed without bodies");
  expect(program.traits.front().methods[1].type_parameters.size() == 1,
         "trait methods can be generic");
  expect(program.traits.front().methods[2].is_consuming,
         "trait methods can declare a consuming ownership contract");
  expect(program.classes[1].implemented_traits.size() == 1,
         "class trait implementations are parsed");
  expect(program.functions.front().type_constraints.size() == 1,
         "generic trait constraints are parsed");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "traits");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("call ptr @Sequence__int__iterator") != std::string::npos,
         "a constrained call is statically dispatched to the concrete method");

  expect_compile_error("trait Duplicate[T, T] {} def main() : int { return 0 }",
                       "type parameter 'T' is already declared");
  expect_compile_error(
      "trait Duplicate { def value() : int def value() : int } "
      "def main() : int { return 0 }",
      "trait method 'value' is already declared");
  expect_compile_error("trait Invalid { def value() : Missing } "
                       "def main() : int { return 0 }",
                       "unknown type 'Missing'");
  expect_compile_error("trait Named { def name() : string } "
                       "class Missing() extends Named {} "
                       "def main() : int { return 0 }",
                       "does not implement trait method 'Named.name'");
  expect_compile_error(
      "trait Named { def name() : string } "
      "class Wrong() extends Named { def name() : int { return 1 } } "
      "def main() : int { return 0 }",
      "signature incompatible");
  expect_compile_error("trait Named { def name() : string } "
                       "class Hidden() extends Named { "
                       "private def name() : string { return \"hidden\" } } "
                       "def main() : int { return 0 }",
                       "private method 'name' cannot implement");
  expect_compile_error("trait Named { def name() : string } "
                       "def visit[T <: Named](value : T) : int { return 1 } "
                       "def main() : int { return visit[int](1) }",
                       "type 'int' does not satisfy constraint 'Named'");
  expect_compile_error("def visit[T <: Missing](value : T) : int { return 1 } "
                       "def main() : int { return 0 }",
                       "unknown trait 'Missing'");
  expect_compile_error(
      "trait Resource { consume def close() : Unit } "
      "class File() extends Resource { def close() : Unit {} } "
      "def main() : int { return 0 }",
      "ownership contract incompatible");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "generic trait signatures are parsed and validated\n";
  return 0;
}
