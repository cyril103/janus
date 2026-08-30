#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <iostream>
#include <cfenv>
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
const opcode : uint = 0xA2_0A
const sprite : ubyte = 0b1111_0000
const masked : ubyte = (sprite | ubyte(15)) & ubyte(63) ^ ubyte(3)
const shifted : ubyte = masked << 1 >> 1
const signedShift : byte = byte(-128) >> 7
const signedShiftLeft8 : byte = byte(64) << 1
const signedShiftRight16 : short = short(-32768) >> 15
const signedShiftRight32 : int = -0x8000_0000 >> 31
const signedMinimum64 : long = -0x8000_0000_0000_0000
const signedShiftRight64 : long = signedMinimum64 >> 63
const signedAnd : byte = byte(-1) & byte(1)
const signedOr : short = short(-32768) | short(1)
const signedXor : int = int(-1) ^ int(1)

const def align(value : usize, boundary : usize) : usize {
    return ((value + boundary - usize(1)) / boundary) * boundary
}

const def increment(value : int) : int { return value + 1 }

const bufferSize : usize = align(usize(1000), usize(64))
const piped : int = 41 |> increment
staticAssert(capacity == 2000)
staticAssert(bufferSize == usize(1024), "alignment must remain stable")
staticAssert(piped == 42, "pipeline desugaring is available to constants")
staticAssert(opcode == 41_482, "hexadecimal const evaluation must match decimal")
staticAssert(sprite == ubyte(240), "binary const evaluation must match decimal")
staticAssert(shifted == ubyte(60), "bitwise constants preserve ubyte width")
staticAssert(signedShift == byte(-1), "signed constant right shift is arithmetic")
staticAssert(signedShift + byte(1) == byte(0), "signed byte shift result stays canonical")
staticAssert(signedShiftLeft8 + byte(1) == byte(-127), "signed byte left shift keeps its bit pattern")
staticAssert(signedShiftRight16 + short(1) == short(0), "signed short shift result stays canonical")
staticAssert(signedShiftRight32 + 1 == 0, "signed int shift result stays canonical")
staticAssert(signedShiftRight64 + long(1) == long(0), "signed long shift result stays canonical")
staticAssert(signedAnd + byte(1) == byte(2), "signed bitwise and stays canonical")
staticAssert(signedOr + short(32767) == short(0), "signed bitwise or stays canonical")
staticAssert(signedXor + int(2) == int(0), "signed bitwise xor stays canonical")

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
  const auto float_analysis = analyzer.analyze(float_program);
  expect(std::get<double>(float_analysis.global_constant_values.at("x").data) ==
             16777216.0,
         "binary32 value is rounded in the semantic constant representation");
  for (const int rounding_mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    std::fesetround(rounding_mode);
    janus::frontend::Parser mode_parser{
        "const x : float = 16777216.0f + 1.0f\n"
        "staticAssert(x == 16777216.0f)\n"
        "def main() : int { return 0 }"};
    const auto mode_analysis = analyzer.analyze(mode_parser.parse_program());
    expect(std::get<double>(
               mode_analysis.global_constant_values.at("x").data) ==
               16777216.0,
           "binary32 evaluation is independent of the host rounding mode");
  }
  std::fesetround(FE_TONEAREST);
  llvm::LLVMContext float_context;
  janus::backend::llvm::IrGenerator float_generator{float_context};
  static_cast<void>(float_generator.generate(float_program, "float_constant"));

  expect_compile_error(
      "var runtime : int = 1\nconst invalid : int = runtime\n"
      "def main() : int { return 0 }",
      "constant 'invalid' cannot depend on mutable global 'runtime'");
  expect_compile_error(
      "val ordinary : int = 7\nconst invalid : int = ordinary\n"
      "def main() : int { return invalid }",
      "cannot depend on non-constant global 'ordinary'");
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
      "const invalid : ubyte = ubyte(1) << 8\ndef main() : int { return 0 }",
      "shift count must be less than the left operand width");
  expect_compile_error(
      "const invalid : ushort = ushort(1) >> 17\ndef main() : int { return 0 }",
      "shift count must be less than the left operand width");
  expect_compile_error(
      "const max : ulong = 18446744073709551615\n"
      "const invalid : ulong = max * max\ndef main() : int { return 0 }",
      "constant integer expression overflows type 'ulong'");
  expect_compile_error(
      "const invalid : int = int(1.0e300)\ndef main() : int { return 0 }",
      "floating constant conversion overflows type 'int'");
  expect_compile_error(
      "const invalid : float = 1.0f / 0.0f\ndef main() : int { return 0 }",
      "floating constant expression is not finite");
  expect_compile_error(
      "const invalid : double = 0.0 / 0.0\ndef main() : int { return 0 }",
      "floating constant expression is not finite");
  expect_compile_error(
      "const x : float = 3.4e38f * 2.0f\ndef main() : int { return 0 }",
      "floating constant expression overflows type 'float'");
  expect(janus::constant::canonical_serialize(
             {&janus::Type::float_type(), 1.0}) == "float:f32:0x3f800000",
         "binary32 constants serialize as exact IEEE bits");
  expect(janus::constant::canonical_serialize(
             {&janus::Type::double_type(), 1.0}) ==
             "double:f64:0x3ff0000000000000",
         "binary64 constants serialize as exact IEEE bits");
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
  janus::frontend::Parser homonymous_enum_parser{R"(
enum Zero { int, Other }
enum Pair { int(int, int), Other }
const zero : Zero = Zero.int()
const pair : Pair = Pair.int(20, 22)
const zeroAnswer : int = match zero { int() => 7, Other => 0 }
const pairAnswer : int = match pair { int(left, right) => left + right, Other => 0 }
val plannedZero : int = match zero { int() => zeroAnswer, Other => 0 }
val plannedPair : int = match pair { int(left, right) => left + right, Other => 0 }
staticAssert(zeroAnswer == 7)
staticAssert(pairAnswer == 42)
def runtimeZero(value : Zero) : int { return match value { int() => 7, Other => 0 } }
def runtimePair(value : Pair) : int { return match value { int(left, right) => left + right, Other => 0 } }
def scalar(value : int) : int { return match value { int(42) => 1, _ => 0 } }
def main() : int { return plannedZero + plannedPair + runtimeZero(zero) + runtimePair(pair) + scalar(42) }
)"};
  const janus::ast::Program homonymous_enum_program =
      homonymous_enum_parser.parse_program();
  const auto initializer_is_constant = [&](std::string_view name) {
    const auto found = std::find_if(
        homonymous_enum_program.globals.begin(),
        homonymous_enum_program.globals.end(), [&](const auto &global) {
          return global.declaration.name == name;
        });
    return found != homonymous_enum_program.globals.end() &&
           janus::constant::is_constant_expression(
               *found->declaration.initializer);
  };
  expect(initializer_is_constant("zeroAnswer") &&
             initializer_is_constant("pairAnswer"),
         "homonymous enum patterns pass structural constant classification");
  const janus::semantic::AnalysisResult homonymous_enum_analysis =
      analyzer.analyze(homonymous_enum_program);
  expect(homonymous_enum_analysis.global_constant_values.contains("zeroAnswer") &&
             homonymous_enum_analysis.global_constant_values.contains("pairAnswer") &&
             homonymous_enum_analysis.global_constant_values.contains("plannedZero") &&
             homonymous_enum_analysis.global_constant_values.contains("plannedPair"),
         "homonymous enum patterns are classified in const and plan contexts");
  llvm::LLVMContext homonymous_enum_context;
  janus::backend::llvm::IrGenerator homonymous_enum_generator{
      homonymous_enum_context};
  static_cast<void>(homonymous_enum_generator.generate(
      homonymous_enum_program, "homonymous_enum_constant_match"));
  janus::frontend::Parser homonymous_binding_scope_parser{R"(
enum E { int(int) }
var x : int = 0
val selected : int = match E.int(7) { int(x) => x }
const def unwrap(e : E) : int { return match e { int(x) => x } }
const answer : int = unwrap(E.int(7))
def main() : int { return selected + answer }
)"};
  const janus::ast::Program homonymous_binding_scope_program =
      homonymous_binding_scope_parser.parse_program();
  const janus::semantic::AnalysisResult homonymous_binding_scope_analysis =
      analyzer.analyze(homonymous_binding_scope_program);
  expect(homonymous_binding_scope_analysis.global_constant_values.contains(
             "selected") &&
             homonymous_binding_scope_analysis.global_constant_values.contains(
                 "answer"),
         "enum pattern bindings shadow mutable globals in dependency and purity visitors");
  janus::frontend::Parser literal_match_parser{R"(
const opcode : uint = uint(0x8001)
const decoded : int = match opcode {
    uint(0x8000) => 0,
    _ if (opcode & uint(0x000F)) == uint(1) => 1,
    _ => -1
}
staticAssert(decoded == 1)
def main() : int { return decoded }
)"};
  const janus::ast::Program literal_match_program =
      literal_match_parser.parse_program();
  const janus::semantic::AnalysisResult literal_match_analysis =
      analyzer.analyze(literal_match_program);
  expect(literal_match_analysis.global_constant_values.contains("decoded"),
         "constant evaluator supports literal patterns and guards");
  llvm::LLVMContext literal_match_context;
  janus::backend::llvm::IrGenerator literal_match_generator{
      literal_match_context};
  const std::unique_ptr<llvm::Module> literal_match_module =
      literal_match_generator.generate(literal_match_program,
                                       "literal_constant_match");
  std::string literal_match_ir;
  llvm::raw_string_ostream literal_match_output{literal_match_ir};
  literal_match_module->print(literal_match_output, nullptr);
  literal_match_output.flush();
  expect(literal_match_ir.find("ret i32 1") != std::string::npos,
         "constant and backend literal match results have parity");
  expect_compile_error(
      "def runtime() : int { return 1 }\n"
      "val selected : int = match 1 { int(runtime()) => 1, _ => 0 }\n"
      "def main() : int { return selected }",
      "match pattern must be a literal");
  janus::frontend::Parser dynamic_guard_parser{R"(
def runtime() : bool { return true }
val selected : int = match 1 { _ if runtime() => 7, _ => 0 }
def main() : int { return selected }
)"};
  const janus::ast::Program dynamic_guard_program =
      dynamic_guard_parser.parse_program();
  static_cast<void>(analyzer.analyze(dynamic_guard_program));
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
       .constant_recursion_budget = 2, .target = {}});
  expect_compile_error(
      "const def identity(value : int) : int { return value }\n"
      "const answer : int = identity(1)\ndef main() : int { return 0 }",
      "constant evaluation step budget exceeded (0)",
      {.require_entry_point = true, .constant_step_budget = 0, .target = {}});
  expect_compile_error(
      "const result : int = 1 + 1\ndef main() : int { return result }",
      "constant evaluation step budget exceeded (0)",
      {.require_entry_point = true, .constant_step_budget = 0, .target = {}});
  expect_compile_error(
      "const text : string = \"123456789\"\ndef main() : int { return 0 }",
      "constant value size budget exceeded (8 bytes)",
      {.require_entry_point = true, .constant_value_size_budget = 8,
       .target = {}});
  expect_compile_error(
      "const first : string = \"1234\"\nconst second : string = \"5678\"\n"
      "def main() : int { return 0 }",
      "constant evaluation memory budget exceeded (20 bytes)",
      {.require_entry_point = true, .constant_memory_budget = 20,
       .target = {}});
  expect_compile_error(
      "def main() : int {\n"
      "    const text : string = \"arbitrarily large local constant\"\n"
      "    return 0\n}",
      "constant evaluation memory budget exceeded (1 bytes)",
      {.require_entry_point = true, .constant_memory_budget = 1,
       .constant_value_size_budget = 1, .target = {}});

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
  janus::frontend::Parser generic_wide_parser{
      "const def identity[T](value : T) : T { return value }\n"
      "const wide : long = identity[long](2147483648)\n"
      "def main() : int { return int(wide) }"};
  static_cast<void>(analyzer.analyze(generic_wide_parser.parse_program()));
  llvm::LLVMContext generic_context;
  janus::backend::llvm::IrGenerator generic_generator{generic_context};
  static_cast<void>(generic_generator.generate(generic_program,
                                               "generic_constant"));

  constexpr std::string_view target_source =
      "const wide : usize = 4294967296\n"
      "def main() : int { return 0 }";
  expect_compile_error(
      target_source, "overflows type 'usize'",
      {.require_entry_point = true,
       .target = {.triple = "i686-unknown-linux-gnu", .pointer_width = 32}});
  janus::frontend::Parser target64_parser{target_source};
  const auto target64 = analyzer.analyze(
      target64_parser.parse_program(),
      {.require_entry_point = true,
       .target = {.triple = "x86_64-unknown-linux-gnu", .pointer_width = 64}});
  expect(target64.target.pointer_width == 64 &&
             target64.global_constant_values.at("wide").type->bit_width() == 64,
         "constant evaluation uses the explicit 64-bit target model");
  janus::frontend::Parser target32_const_def_parser{
      "const def huge() : usize { return usize(1) }\n"
      "const x : usize = huge()\ndef main() : int { return 0 }"};
  const auto target32_const_def = analyzer.analyze(
      target32_const_def_parser.parse_program(),
      {.require_entry_point = true,
       .target = {.triple = "i686-unknown-linux-gnu", .pointer_width = 32}});
  expect(target32_const_def.global_constant_values.at("x").type->bit_width() ==
             32,
         "const def usize values preserve the target pointer width");
  janus::frontend::Parser target32_parser{
      "const width : usize = 32\n"
      "def preserve(value : usize) : usize { return value }\n"
      "def main() : int { return int(preserve(width)) }"};
  const janus::ast::Program target32_program = target32_parser.parse_program();
  llvm::LLVMContext target32_context;
  janus::backend::llvm::IrGenerator target32_generator{
      target32_context,
      {.triple = "i686-unknown-linux-gnu", .pointer_width = 32}};
  const auto target32_module =
      target32_generator.generate(target32_program, "target32");
#if LLVM_VERSION_MAJOR >= 21
  const std::string target32_triple =
      target32_module->getTargetTriple().str();
#else
  const std::string target32_triple = target32_module->getTargetTriple();
#endif
  expect(target32_triple == "i686-unknown-linux-gnu",
         "backend emits the explicit target triple");
  std::string target32_ir;
  llvm::raw_string_ostream target32_output{target32_ir};
  target32_module->print(target32_output, nullptr);
  target32_output.flush();
  const std::size_t preserve = target32_ir.find("preserve");
  expect(preserve != std::string::npos &&
             target32_ir.substr(preserve, 160).find("i32") != std::string::npos,
         "backend lowers usize as i32 for an explicit 32-bit target");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "compile-time constants are deterministic and storage-free\n";
  return 0;
}
