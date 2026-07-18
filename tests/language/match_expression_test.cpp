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
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "match error contains the expected explanation");
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
    val option : Option[int] = Option.Some[int](42)
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
      "match requires an enum value");
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

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "match expressions destructure enum payloads\n";
  return 0;
}
