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
                          std::string_view expected_message,
                          janus::semantic::AnalysisOptions options = {}) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program(), options));
    expect(false, std::string{"invalid constant program must fail: "} +
                      std::string{source});
  } catch (const janus::CompileError &error) {
    if (std::string_view{error.what()}.find(expected_message) ==
        std::string_view::npos) {
      std::cerr << "FAILED: expected diagnostic containing '"
                << expected_message << "', got '" << error.what() << "'\n";
      ++failures;
    }
  }
}

} // namespace

int main() {
  janus::frontend::Parser parser{R"(
const width : int = 80
const height : int = 25
const capacity : int = width * height
const selected : int = if capacity == 2000 { 7 } else { 9 }

const def align(value : usize, boundary : usize) : usize {
    return ((value + boundary - usize(1)) / boundary) * boundary
}

const bufferSize : usize = align(usize(1000), usize(64))
staticAssert(capacity == 2000)
staticAssert(bufferSize == usize(1024), "alignment must remain stable")

def main() : int {
    val runtime : usize = align(usize(5), usize(4))
    var compatibility : int = selected
    return int(runtime) + compatibility
}
)"};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "compile_time_constants");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("__janus_global_entry__capacity") == std::string::npos,
         "a scalar const does not allocate runtime global storage");
  expect(ir.find("__janus_global_entry__bufferSize") == std::string::npos,
         "a const-def result does not allocate runtime global storage");
  expect(ir.find("define") != std::string::npos &&
             ir.find("align") != std::string::npos,
         "const def remains callable at runtime");
  expect(ir.find("ret i32 15") == std::string::npos,
         "ordinary val/var execution is not globally folded away");

  janus::frontend::Parser local_parser{R"(
const first : int = 99
def main() : int {
    const first : int = 20
    const second : int = first + 22
    return second
}
)"};
  const janus::ast::Program local_program = local_parser.parse_program();
  static_cast<void>(analyzer.analyze(local_program));
  llvm::LLVMContext local_context;
  janus::backend::llvm::IrGenerator local_generator{local_context};
  const std::unique_ptr<llvm::Module> local_module =
      local_generator.generate(local_program, "local_compile_time_constants");
  std::string local_ir;
  llvm::raw_string_ostream local_output{local_ir};
  local_module->print(local_output, nullptr);
  local_output.flush();
  expect(local_ir.find("alloca") == std::string::npos,
         "dependent local constants must be substituted without storage");
  expect(local_ir.find("ret i32 42") != std::string::npos,
         "local constants use the nearest lexical constant value");

  janus::frontend::Parser body_parser{R"(
const def magnitude(value : int) : int {
    const zero : int = 0
    if value > zero {
        return value
    } else {
        return zero - value
    }
}
const def factorial(value : int) : int {
    if value <= 1 {
        return 1
    } else {
        return value * factorial(value - 1)
    }
}
const answer : int = magnitude(42)
staticAssert(answer == 42)
staticAssert(factorial(5) == 120)
def main() : int { return answer }
)"};
  static_cast<void>(analyzer.analyze(body_parser.parse_program()));

  janus::frontend::Parser float_parser{R"(
const x : float = 16777216.0f + 1.0f
def main() : int { return if x == 16777216.0f { 0 } else { 1 } }
)"};
  const janus::ast::Program float_program = float_parser.parse_program();
  static_cast<void>(analyzer.analyze(float_program));
  llvm::LLVMContext float_context;
  janus::backend::llvm::IrGenerator float_generator{float_context};
  static_cast<void>(float_generator.generate(float_program, "float_constant"));

  expect_compile_error(
      "var runtime : int = 1\nconst invalid : int = runtime\n"
      "def main() : int { return 0 }",
      "constant 'invalid' cannot depend on mutable global 'runtime'");
  expect_compile_error(
      "var state : int = 7\n"
      "const def impure() : int { return state }\n"
      "def main() : int { return 0 }",
      "const def 'impure' cannot observe mutable global 'state'");
  expect_compile_error(
      "def io() : int { return 1 }\n"
      "const def nested() : int { return io() }\n"
      "const def outer() : int { return nested() }\n"
      "def main() : int { return 0 }",
      "cannot call non-constant function 'io'");
  expect_compile_error(
      "const first : int = second\nconst second : int = first\n"
      "def main() : int { return 0 }",
      "cyclic constant definition");
  expect_compile_error(
      "const first : int = second\nconst second : int = third\n"
      "const third : int = first\ndef main() : int { return 0 }",
      "first -> second -> third -> first");
  expect_compile_error(
      "const invalid : byte = 127 + 1\ndef main() : int { return 0 }",
      "constant integer expression overflows type 'byte'");
  expect_compile_error(
      "const invalid : int = 1 / 0\ndef main() : int { return 0 }",
      "division by zero in constant expression");
  expect_compile_error(
      "const max : ulong = 18446744073709551615\n"
      "const invalid : ulong = max * max\ndef main() : int { return 0 }",
      "constant integer expression overflows type 'ulong'");
  expect_compile_error(
      "const invalid : int = int(1.0e300)\ndef main() : int { return 0 }",
      "floating constant conversion overflows type 'int'");
  janus::frontend::Parser dead_branch_parser{
      "const safe : int = if true { 42 } else { 1 / 0 }\n"
      "staticAssert(safe == 42)\ndef main() : int { return 0 }"};
  static_cast<void>(analyzer.analyze(dead_branch_parser.parse_program()));

  janus::frontend::Parser match_parser{R"(
enum Option[T] { Some(T), None }
const selected : Option[int] = Option.Some[int](42)
const answer : int = match selected {
    Some(value) => value,
    None => 1 / 0
}
staticAssert(answer == 42)
def main() : int { return answer }
)"};
  const janus::ast::Program match_program = match_parser.parse_program();
  const janus::semantic::AnalysisResult match_analysis =
      analyzer.analyze(match_program);
  expect(match_analysis.global_constant_values.contains("selected"),
         "analysis evaluates enum constants through constructor resolution");
  expect(match_analysis.global_constant_values.contains("answer"),
         "analysis evaluates constant match payload bindings");
  llvm::LLVMContext match_context;
  janus::backend::llvm::IrGenerator match_generator{match_context};
  const std::unique_ptr<llvm::Module> match_module =
      match_generator.generate(match_program, "constant_match");
  std::string match_ir;
  llvm::raw_string_ostream match_output{match_ir};
  match_module->print(match_output, nullptr);
  match_output.flush();
  expect(match_ir.find("ret i32 42") != std::string::npos,
         "backend folds the selected match arm and does not evaluate dead arms");
  expect_compile_error(
      "def runtime() : bool { return true }\nstaticAssert(runtime())\n"
      "def main() : int { return 0 }",
      "static assertion condition is not a constant expression");
  expect_compile_error(
      "staticAssert(1 == 2, \"numbers disagree\")\n"
      "def main() : int { return 0 }",
      "static assertion failed: numbers disagree");
  expect_compile_error(
      "const def recurse(value : int) : int { return recurse(value + 1) }\n"
      "const answer : int = recurse(0)\ndef main() : int { return 0 }",
      "constant evaluation recursion budget exceeded (2)",
      {.require_entry_point = true, .constant_step_budget = 100,
       .constant_recursion_budget = 2});
  expect_compile_error(
      "const def identity(value : int) : int { return value }\n"
      "const answer : int = identity(1)\ndef main() : int { return 0 }",
      "constant evaluation step budget exceeded (0)",
      {.require_entry_point = true, .constant_step_budget = 0});
  expect_compile_error(
      "const text : string = \"123456789\"\ndef main() : int { return 0 }",
      "constant value size budget exceeded (8 bytes)",
      {.require_entry_point = true, .constant_value_size_budget = 8});
  expect_compile_error(
      "const first : string = \"1234\"\nconst second : string = \"5678\"\n"
      "def main() : int { return 0 }",
      "constant evaluation memory budget exceeded (20 bytes)",
      {.require_entry_point = true, .constant_memory_budget = 20});

  janus::frontend::Parser generic_parser{R"(
const def identity[T](value : T) : T { return value }
struct Pair(val left : int, val right : int) derives Copy {}
enum Flag derives Copy { On, Off }
const integer : int = identity[int](42)
const truth : bool = identity[bool](true)
const pair : Pair = identity[Pair](new Pair(20, 22))
const flag : Flag = identity[Flag](Flag.On)
staticAssert(integer == 42)
staticAssert(truth)
def main() : int { return identity[int](integer) }
)"};
  const janus::ast::Program generic_program = generic_parser.parse_program();
  static_cast<void>(analyzer.analyze(generic_program));
  llvm::LLVMContext generic_context;
  janus::backend::llvm::IrGenerator generic_generator{generic_context};
  static_cast<void>(generic_generator.generate(generic_program,
                                               "generic_constant"));

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "compile-time constants are deterministic and storage-free\n";
  return 0;
}
