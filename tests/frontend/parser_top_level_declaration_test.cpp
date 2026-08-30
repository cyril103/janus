#include "janus/frontend/parser.hpp"
#include "janus/frontend/lexer.hpp"

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

} // namespace

int main() {
  {
    janus::frontend::Lexer lexer{
        "+= -= *= /= %= &= |= ^= <<= >>= = << >> |> | ||"};
    const janus::frontend::TokenKind expected[] = {
        janus::frontend::TokenKind::PlusEqual,
        janus::frontend::TokenKind::MinusEqual,
        janus::frontend::TokenKind::StarEqual,
        janus::frontend::TokenKind::SlashEqual,
        janus::frontend::TokenKind::PercentEqual,
        janus::frontend::TokenKind::AmpersandEqual,
        janus::frontend::TokenKind::PipeEqual,
        janus::frontend::TokenKind::CaretEqual,
        janus::frontend::TokenKind::ShiftLeftEqual,
        janus::frontend::TokenKind::ShiftRightEqual,
        janus::frontend::TokenKind::Equal,
        janus::frontend::TokenKind::ShiftLeft,
        janus::frontend::TokenKind::ShiftRight,
        janus::frontend::TokenKind::PipeGreater,
        janus::frontend::TokenKind::Pipe,
        janus::frontend::TokenKind::PipePipe,
    };
    for (const auto kind : expected)
      expect(lexer.next().kind == kind,
             "compound assignment operators use longest-match tokenization");
  }

  {
    janus::frontend::Parser pipeline_parser{R"(
def increment(value : int) : int { return value + 1 }
def add(value : int, amount : int) : int { return value + amount }
def pipeline() : int { return 1 |> increment |> add(40) }
)"};
    const janus::ast::Program pipeline_program =
        pipeline_parser.parse_program();
    const auto *statement = std::get_if<janus::ast::ReturnStatement>(
        &pipeline_program.functions.back().body.front());
    const auto *outer = statement == nullptr || !statement->expression
                            ? nullptr
                            : std::get_if<janus::ast::CallExpression>(
                                  &statement->expression->value);
    const auto *inner = outer == nullptr || outer->arguments.empty()
                            ? nullptr
                            : std::get_if<janus::ast::CallExpression>(
                                  &outer->arguments.front()->value);
    expect(outer != nullptr && outer->callee == "add" &&
               outer->arguments.size() == 2,
           "pipeline prepends its left value to an existing call");
    expect(inner != nullptr && inner->callee == "increment" &&
               inner->arguments.size() == 1,
           "pipeline is left-associative and calls a bare function");
  }
  {
    bool invalid_target_rejected = false;
    try {
      janus::frontend::Parser invalid_pipeline{
          "def invalid() : int { return 1 |> 2 }"};
      static_cast<void>(invalid_pipeline.parse_program());
    } catch (const janus::CompileError &error) {
      invalid_target_rejected =
          std::string_view{error.what()}.find(
              "pipeline right-hand side must be a function or function call") !=
          std::string_view::npos;
    }
    expect(invalid_target_rejected,
           "pipeline reports a targeted error for a non-callable target");
  }

  janus::frontend::Parser parser{R"(
module sample
import std.array
val answer : int = 42
var requests : usize = 0
var pending : int
private val secret : bool = true
def main() : int { return answer }
)"};
  const janus::ast::Program program = parser.parse_program();

  expect(program.globals.size() == 4, "four globals are parsed");
  if (program.globals.size() == 4) {
    const auto &answer = program.globals[0];
    expect(answer.declaration.name == "answer", "global val keeps its name");
    expect(!answer.declaration.is_mutable, "global val is immutable");
    expect(answer.declaration.initializer.has_value(),
           "global val keeps its initializer");
    expect(answer.module_name == "sample",
           "global val keeps its declaring module");

    const auto &requests = program.globals[1];
    expect(requests.declaration.is_mutable, "global var is mutable");
    expect(requests.declaration.initializer.has_value(),
           "initialized global var keeps its initializer");

    const auto &pending = program.globals[2];
    expect(!pending.declaration.initializer.has_value(),
           "parser represents an uninitialized global var");

    const auto &secret = program.globals[3];
    expect(secret.declaration.is_private, "private global keeps its visibility");
  }

  janus::frontend::Parser array_parser{
      "def literals() : int { val values : Array[int] = [1, 2, 3] "
      "val empty : Array[int] = [] return 0 }"};
  const janus::ast::Program arrays = array_parser.parse_program();
  const auto &body = arrays.functions.front().body;
  const auto *values = std::get_if<janus::ast::ValueDeclaration>(&body[0]);
  const auto *literal = values == nullptr || !values->initializer
                            ? nullptr
                            : std::get_if<janus::ast::ArrayLiteralExpression>(
                                  &values->initializer->value);
  expect(literal != nullptr && literal->elements.size() == 3,
         "non-empty array literals retain their ordered elements");
  const auto *empty = std::get_if<janus::ast::ValueDeclaration>(&body[1]);
  const auto *empty_literal = empty == nullptr || !empty->initializer
                                  ? nullptr
                                  : std::get_if<janus::ast::ArrayLiteralExpression>(
                                        &empty->initializer->value);
  expect(empty_literal != nullptr && empty_literal->elements.empty(),
         "empty array literals are represented explicitly");

  bool missing_close_rejected = false;
  try {
    janus::frontend::Parser malformed{
        "def bad() : int { val values : Array[int] = [1, 2 return 0 }"};
    static_cast<void>(malformed.parse_program());
  } catch (const janus::CompileError &error) {
    missing_close_rejected =
        std::string_view{error.what()}.find("expected ']' after array literal") !=
        std::string_view::npos;
  }
  expect(missing_close_rejected, "array literal delimiter errors are targeted");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "top-level val/var declarations are represented in the AST\n";
  return 0;
}
