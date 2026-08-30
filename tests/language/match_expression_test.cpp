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
    expect(false, "invalid match expression must fail");
  } catch (const janus::CompileError &error) {
    if (std::string_view{error.what()}.find(expected_message) ==
        std::string_view::npos) {
      std::cerr << "FAILED: expected match diagnostic '" << expected_message
                << "', got '" << error.what() << "'\n";
      ++failures;
    }
  }
}

void expect_valid(std::string_view source, std::string_view message) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << message << ": " << error.what() << '\n';
    ++failures;
  }
}

void expect_codegen_valid(std::string_view source, std::string_view message) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    llvm::LLVMContext context;
    janus::backend::llvm::IrGenerator generator{context};
    static_cast<void>(generator.generate(program, "recursive_patterns"));
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << message << ": " << error.what() << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
enum Option[T] {
    Some(T),
    None
}

def main() : int {
    val option = Option.Some(42)
    val empty : Option[int] = Option.None()
    return match option {
        Some(value) => value,
        None => 0
    }
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.functions.size() == 1, "match source is parsed");
  const auto *return_statement = std::get_if<janus::ast::ReturnStatement>(
      &program.functions.front().body.back());
  expect(return_statement != nullptr &&
             std::holds_alternative<janus::ast::MatchExpression>(
                 return_statement->expression->value),
         "match is represented as an expression");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "match_expression");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("switch i32") != std::string::npos,
         "match lowers to an LLVM switch");
  expect(ir.find("value.payload") != std::string::npos,
         "variant payload is destructured");
  expect(ir.find("phi i32") != std::string::npos,
         "match arms merge into one value");

  expect_compile_error(
      "enum E { A } def main() : int { return match 1 { A => 0 } }",
      "literal match requires a literal or '_' pattern");
  expect_compile_error(
      "enum E { A(int) } def main() : int { val e : E = E.A(1) "
      "return match e { A => 0 } }",
      "pattern binds 0");
  expect_compile_error("enum E { A } def main() : int { val e : E = E.A() "
                       "return match e { Missing => 0 } }",
                       "has no case 'Missing'");
  expect_compile_error("enum E { A, B } def main() : int { val e : E = E.A() "
                       "return match e { A => 0, B => 1.0 } }",
                       "match cases must have the same type");
  expect_compile_error("enum E { A, B } def main() : int { val e : E = E.A() "
                       "return match e { A => 0 } }",
                       "non-exhaustive match for enum 'E': missing case(s): B");
  expect_compile_error("enum E { A, B } def main() : int { val e : E = E.A() "
                       "return match e { A => 0, A => 1, B => 2 } }",
                       "match case 'A' is already handled");

  expect_valid(R"(
def classify(value : uint) : int {
    return match value { uint(1) => 10, uint(2) => 20, _ => 0 }
}
def main() : int { return classify(uint(2)) }
)", "integer literal patterns parse and type-check");
  expect_valid(R"(
def classify(value : bool) : int {
    return match value { true => 1, false => 0 }
}
def main() : int { return classify(true) }
)", "boolean literal patterns are exhaustive");
  expect_valid(R"(
def classify(value : string) : int {
    return match value { "chip8" => 8, _ => 0 }
}
def main() : int { return classify("chip8") }
)", "string literal patterns are supported");
  expect_valid(R"(
def classify(value : double) : int {
    return match value { 1.5 => 15, _ => 0 }
}
def main() : int { return classify(1.5) }
)", "floating literal patterns are supported");
  expect_valid(R"(
def classify(value : bool) : int {
    return match value { bool(1) => 1, false => 0 }
}
def main() : int { return classify(true) }
)", "converted boolean patterns contribute to exhaustiveness");
  expect_valid(R"(
enum Opcode { Family(uint) }
def classify(opcode : Opcode) : int {
    return match opcode {
        Family(value) if value == uint(1) => 1,
        Family(value) => 0
    }
}
def main() : int { return classify(Opcode.Family(uint(1))) }
)", "guards see enum pattern bindings");
  expect_valid(R"(
enum E { int(int) }
def unwrap(e : E) : int { return match e { int(value) => value } }
def main() : int { return unwrap(E.int(42)) }
)", "enum constructors may share names with literal conversions");
  expect_compile_error(
      "def main() : int { return match 1 { true => 1, _ => 0 } }",
      "literal pattern type 'bool' does not match scrutinee type 'int'");
  expect_compile_error(
      "enum E { A(int) } def main() : int { val e = E.A(1) "
      "return match e { A(value) if value => 1, A(value) => 0 } }",
      "match guard must have type 'bool'");
  expect_compile_error(R"(
class Resource(val identifier : int) { destructor { println(identifier) } }
enum Slot { Occupied(Resource), Empty }
def burn(value : Resource) : bool { delete value return false }
def main() : int {
    val slot : Slot = Slot.Occupied(new Resource(9))
    return match move slot {
        Occupied(value) if burn(move value) => 1,
        Occupied(value) => 2,
        Empty => 0
    }
}
)", "pattern binding 'value' cannot be transferred or destroyed in a match guard");
  expect_compile_error(R"(
class Resource(val identifier : int) { destructor { println(identifier) } }
enum Slot { Occupied(Resource), Empty }
def rejects(cleanup : () => Unit) : bool { return false }
def main() : int {
    val slot : Slot = Slot.Occupied(new Resource(9))
    return match move slot {
        Occupied(value) if rejects(owningCapture[Resource](value, () => println(value.identifier))) => 1,
        Occupied(value) => 2,
        Empty => 0
    }
}
)", "pattern binding 'value' cannot be transferred or destroyed in a match guard");
  expect_compile_error(R"(
enum Slot { Occupied(Ptr[int]), Empty }
def main() : int {
    val slot : Slot = Slot.Occupied(alloc[int](usize(1)))
    return match move slot {
        Occupied(value) if adoptReallocation[int](value, null[int]()) => 1,
        Occupied(value) => 2,
        Empty => 0
    }
}
)", "pattern binding 'value' cannot be transferred or destroyed in a match guard");
  expect_compile_error(R"(
enum Slot { Occupied(Ptr[int]), Empty }
def main() : int {
    val slot : Slot = Slot.Occupied(alloc[int](usize(1)))
    return match move slot {
        Occupied(value) if free(value) => 1,
        Occupied(value) => 2,
        Empty => 0
    }
}
)", "pattern binding 'value' cannot be transferred or destroyed in a match guard");
  expect_compile_error(R"(
enum Slot { Occupied(Ptr[int]), Empty }
def main() : int {
    val slot : Slot = Slot.Occupied(alloc[int](usize(1)))
    return match move slot {
        Occupied(value) if freeStorage(value) => 1,
        Occupied(value) => 2,
        Empty => 0
    }
}
)", "pattern binding 'value' cannot be transferred or destroyed in a match guard");
  expect_valid(R"(
class Resource(val identifier : int) { destructor { println(identifier) } }
enum Slot { Occupied(Resource), Empty }
def dispose(value : Resource) : int { val result = value.identifier delete value return result }
def main() : int {
    val slot : Slot = Slot.Occupied(new Resource(9))
    return match move slot {
        Occupied(value) if true => dispose(move value),
        Occupied(value) => dispose(move value),
        Empty => 0
    }
}
)", "owning pattern bindings remain transferable after a successful guard");
  expect_compile_error(
      "def main() : int { return match 1 { 1 => 1, 1 => 2, _ => 0 } }",
      "literal pattern '1' is already handled");
  expect_compile_error(
      "def main() : int { return match 1 { 1 => 1, 1 if true => 2, _ => 0 } }",
      "literal pattern '1' is already handled");
  expect_compile_error(
      "def main() : int { return match 1 { 1 => 1, int(1) if true => 2, _ => 0 } }",
      "literal pattern '1' is already handled");
  expect_compile_error(
      "def main() : int { return match 1 { 1 => 1, int(byte(1)) => 2, _ => 0 } }",
      "literal pattern '1' is already handled");
  expect_compile_error(
      "def main() : int { return match true { true => 1, bool(1) => 2, false => 0 } }",
      "literal pattern 'true' is already handled");
  expect_compile_error(
      "enum E { A } def main() : int { val e = E.A() return match e { A => 1, A if true => 2 } }",
      "match case 'A' is already handled");
  expect_compile_error(
      "def main() : int { val expected = 1 return match 1 { int(expected) => 1, _ => 0 } }",
      "match pattern must be a literal");
  expect_compile_error(
      "def main() : int { return match 3 { 1 + 2 => 1, _ => 0 } }",
      "match pattern must be a literal");
  expect_compile_error("def main() : int { return match 1 { _ => 0, 1 => 1 } }",
                       "match arm is unreachable after wildcard pattern");
  expect_compile_error("def main() : int { return match 1 { 1 if true => 1 } }",
                       "non-exhaustive match");
  expect_compile_error("enum E { A } def main() : int { val e = E.A() "
                       "return match e { A if true => 1 } }",
                       "non-exhaustive match for enum 'E': missing case(s): A");
  expect_compile_error("enum E { A(int) } def main() : int { val e = E.A(1) "
                       "val x = match e { A(value) => value } return value }",
                       "unknown value 'value'");
  expect_codegen_valid(R"(
enum Inner[T] { Value(T), Empty }
enum Outer[T] { Wrapped(Inner[T]), Missing }
def unwrap(value : Outer[int]) : int {
    return match value {
        Wrapped(Value(item)) => item,
        Wrapped(_) => 0,
        Missing => -1
    }
}
def main() : int { return unwrap(Outer.Wrapped(Inner.Value(42))) }
)",
                       "nested generic enum patterns lower recursively");
  expect_codegen_valid(
      R"(
struct Address(val city : int, val zip : int) {}
struct User(val name : int, val address : Address) {}
def cityOf(user : User) : int {
    return match user { User(name, Address(city, _)) as whole => name + city + whole.name }
}
def main() : int { return cityOf(new User(1, new Address(2, 3))) }
)",
      "struct patterns, nested wildcards and aliases lower recursively");
  expect_codegen_valid(
      R"(
enum Choice { Left(int), Right(int), Empty }
def number(value : Choice) : int {
    return match value {
        Left(item) | Right(item) => item,
        Empty => 0
    }
}
def main() : int { return number(Choice.Right(7)) }
)",
      "alternatives with compatible bindings lower recursively");
  expect_compile_error(R"(
enum Choice { Left(int), Right(int) }
def number(value : Choice) : int {
    return match value { Left(item) | Right(other) => item }
}
def main() : int { return 0 }
)",
                       "pattern alternatives must bind exactly the same names");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "match expressions destructure enum payloads\n";
  return 0;
}
