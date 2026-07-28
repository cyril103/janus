#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>

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
    expect(false, "invalid derivation must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "derivation diagnostic identifies the rejected capability");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
struct Point(val x : int, val y : int)
derives Copy, Equality, Hashing, Debug {}

enum Choice derives Equality, Hashing, Debug {
    Number(int),
    Empty
}

def main() : int {
    val left : Point = new Point(2, 3)
    val right : Point = left
    val first : Choice = Choice.Number(5)
    val second : Choice = Choice.Number(5)
    if left == right && first == second {
        debug(left)
        debug(first)
        return 1
    }
    return 0
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "derivation_semantics");
  expect(module != nullptr, "derived operations lower to LLVM");

  expect_compile_error("struct Key(val value : int) derives Hashing {}",
                       "Hashing requires Equality");
  expect_compile_error("class Resource() derives Copy {}",
                       "cannot derive Copy for class 'Resource'");
  expect_compile_error(
      "class Resource() {} struct Packet(val resource : Resource) "
      "derives Copy {}",
      "cannot derive Copy for 'Packet': field 'resource'");
  expect_compile_error(
      "class Resource() {} enum Packet derives Copy { Data(Resource) }",
      "cannot derive Copy for 'Packet': case 'Data' payload 1");
  expect_compile_error(
      "struct Plain(val value : int) {} "
      "struct Wrapped(val value : Plain) derives Equality {}",
      "field 'value' of type 'Plain' does not support Equality");
  expect_compile_error(
      "struct Plain(val value : int) {} "
      "struct Pair[T](val value : T) derives Debug {} "
      "def main() : int { val value : Pair[Plain] = "
      "new Pair[Plain](new Plain(1)) "
      "debug(value) return 0 }",
      "type 'Pair[Plain]' does not derive Debug");
  expect_compile_error(
      "struct Plain(val value : int) {} def main() : int { "
      "val a : Plain = new Plain(1) val b : Plain = new Plain(1) "
      "if a == b { return 1 } return 0 }",
      "does not derive Equality");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "structural derivations are checked and lowered\n";
  return 0;
}
