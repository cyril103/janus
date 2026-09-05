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

void expect_compile_success(std::string_view source,
                            std::string_view expectation) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
  } catch (const janus::CompileError &error) {
    std::cerr << "FAILED: " << expectation << ": " << error.what() << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
class Iterator[T]() {}

trait Iterable[T] {
    def iterator() : Iterator[T]
    def transform[U](value : T, scoped function : (T) => U) : U
    borrow def observe() : borrow T where T <: Copy
    consume def finish() : T
}

trait Sized {
    def size() : usize
}

trait Producer {
    /// Value yielded by this producer.
    type Item
    borrow def next() : Item
}

class Sequence[T](val value : T) extends Iterable[T], Sized {
    def iterator() : Iterator[T] {
        return new Iterator[T]()
    }
    def transform[U](item : T, scoped function : (T) => U) : U {
        return function(move item)
    }
    borrow def observe() : borrow T where T <: Copy {
        return value
    }
    consume def finish() : T {
        return move value
    }
    def size() : usize {
        return usize(1)
    }
}

class NumberProducer() extends Producer {
    type Item = int
    borrow def next() : int { return 42 }
}

def visit[C <: Iterable[int] & Sized](sequence : C) : int {
    val iterator : Iterator[int] = sequence.iterator()
    delete iterator
    return int(sequence.size())
}

def produce[P <: Producer](borrow producer : P) : P.Item {
    return producer.next()
}

def main() : int {
    val producer : NumberProducer = new NumberProducer()
    val bound : int = produce[NumberProducer](producer)
    println(produce[NumberProducer](producer))
    delete producer
    return visit[Sequence[int]](new Sequence[int](5)) + bound
}
)";
  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.traits.size() == 3, "multiple traits are parsed");
  expect(program.traits.front().name == "Iterable",
         "the trait retains its name");
  expect(program.traits.front().type_parameters.size() == 1,
         "generic trait parameters are parsed");
  expect(program.traits.front().methods.size() == 4,
         "trait method signatures are parsed without bodies");
  expect(program.traits.front().methods[1].type_parameters.size() == 1,
         "trait methods can be generic");
  expect(program.traits.front().methods[1].parameters[1].is_scoped,
         "trait methods retain scoped callback effects");
  expect(program.traits.front().methods[2].return_ownership ==
             janus::ast::ReturnOwnership::Borrow,
         "trait methods can declare borrowed return values");
  expect(program.traits.front().methods[2].type_constraints.size() == 1,
         "trait methods retain trailing where constraints");
  expect(program.traits.front().methods[3].is_consuming,
         "trait methods can declare a consuming ownership contract");
  expect(program.classes[1].implemented_traits.size() == 2,
         "multiple class trait implementations are parsed");
  expect(program.functions.front().type_constraints.size() == 2,
         "multiple generic trait constraints are parsed");
  expect(program.traits[2].associated_types.size() == 1 &&
             program.traits[2].associated_types.front().name == "Item",
         "trait associated types are parsed");
  expect(program.classes[2].associated_types.size() == 1 &&
             program.classes[2].associated_types.front().definition->name ==
                 "int",
         "class associated type definitions are parsed");

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
  expect(ir.find("NumberProducer__next") != std::string::npos,
         "an associated return type is normalized for static dispatch");
  expect(ir.find("%produce.result = call i32 @produce__NumberProducer") !=
             std::string::npos,
         "a nested associated return uses the concrete scalar ABI");
  expect(ir.find("call void @janus_print_int(i32 %produce") !=
             std::string::npos,
         "a nested associated return is materialized as its concrete value");

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
  expect_compile_error(
      "trait Contract { def relay[T](action : () => T) : borrow T } "
      "class Wrong() extends Contract { "
      "def relay[U](action : () => U) : U { return action() } } "
      "def main() : int { return 0 }",
      "return ownership differs");
  expect_compile_error(
      "trait Contract { def run(scoped action : () => int) : int } "
      "class Wrong() extends Contract { "
      "def run(action : () => int) : int { return action() } } "
      "def main() : int { return 0 }",
      "scoped contract of parameter 1 differs");
  expect_compile_error(
      "trait Contract { def inspect(borrow value : int) : int } "
      "class Wrong() extends Contract { "
      "def inspect(value : int) : int { return value } } "
      "def main() : int { return 0 }",
      "ownership of parameter 1 differs");
  expect_compile_error(
      "trait Contract { pure def value() : int } "
      "class Wrong() extends Contract { def value() : int { return 1 } } "
      "def main() : int { return 0 }",
      "purity contract differs");
  expect_compile_error(
      "trait Contract { def copy[T](value : T) : T where T <: Copy } "
      "class Wrong() extends Contract { "
      "def copy[U](value : U) : U { return move value } } "
      "def main() : int { return 0 }",
      "generic where constraints differ");
  expect_compile_error(
      "trait Contract { def copy[T](value : T) : T } "
      "class Wrong() extends Contract { "
      "def copy[U](value : U) : U where U <: Copy { return move value } } "
      "def main() : int { return 0 }",
      "generic where constraints differ");
  expect_compile_error(
      "trait Contract { def copy[T](value : T) : T where T <: Copy } "
      "class Wrong() extends Contract { "
      "def copy[U](value : U) : U where U <: Equality { return move value } } "
      "def main() : int { return 0 }",
      "generic where constraints differ");
  expect_compile_success(
      "trait Contract { "
      "def apply[T](scoped action : () => T) : T "
      "where T <: Copy & Equality } "
      "class Exact() extends Contract { "
      "def apply[U](scoped action : () => U) : U "
      "where U <: Equality & Copy { return action() } } "
      "def call[C <: Contract](contract : C) : int { "
      "return contract.apply[int](() => 42) } "
      "def main() : int { return call[Exact](new Exact()) }",
      "equivalent reordered constraints form a valid callable contract");
  expect_compile_error(
      "trait Producer { type Item def next() : Item } "
      "class Missing() extends Producer { def next() : int { return 1 } } "
      "def main() : int { return 0 }",
      "does not define associated type 'Producer.Item'");
  expect_compile_error(
      "trait Producer { type Item } "
      "class Duplicate() extends Producer { type Item = int type Item = int } "
      "def main() : int { return 0 }",
      "associated type 'Item' is already defined");
  expect_compile_error(
      "trait Pair { type Left type Right } "
      "class Cycle() extends Pair { type Left = Right type Right = Left } "
      "def main() : int { return 0 }",
      "cyclic associated type definition");
  expect_compile_error(
      "trait Producer { type Item } "
      "def invalid[T <: Producer](value : T) : T.Missing { return value } "
      "def main() : int { return 0 }",
      "is not provided by a trait constraint");
  expect_compile_error(
      "trait Left { type Item } trait Right { type Item } "
      "def invalid[T <: Left & Right](value : T) : T.Item { return value } "
      "def main() : int { return 0 }",
      "is ambiguous between multiple trait constraints");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "generic trait signatures are parsed and validated\n";
  return 0;
}
