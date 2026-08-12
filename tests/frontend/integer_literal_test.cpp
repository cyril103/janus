#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/lexer.hpp"
#include "janus/frontend/parser.hpp"

#include <iostream>
#include <string_view>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void expect_literal(std::string_view source, std::uint64_t value) {
  janus::frontend::Lexer lexer{source};
  const auto token = lexer.next();
  expect(token.kind == janus::frontend::TokenKind::IntegerLiteral,
         "valid spelling is one integer token");
  expect(token.lexeme == source, "integer token preserves its spelling");
  const std::string program_source =
      "def main() : int { return " + std::string{source} + " }";
  janus::frontend::Parser parser{program_source};
  const auto program = parser.parse_program();
  const auto &expression = *std::get<janus::ast::ReturnStatement>(
                                program.functions[0].body[0])
                                .expression;
  expect(std::get<janus::ast::IntegerLiteralExpression>(expression.value)
                 .magnitude == value,
         "parser canonicalizes the integer value");
}

void expect_invalid(std::string_view spelling, std::string_view message) {
  try {
    janus::frontend::Lexer lexer{spelling};
    static_cast<void>(lexer.next());
    expect(false, "invalid prefixed literal must fail in the lexer");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(message) != std::string_view::npos,
           "invalid literal has a targeted diagnostic");
  }
}

void expect_parse_error(std::string_view spelling, std::string_view message) {
  const std::string source =
      "def main() : int { return " + std::string{spelling} + " }";
  try {
    janus::frontend::Parser parser{source};
    static_cast<void>(parser.parse_program());
    expect(false, "invalid integer magnitude must fail in the parser");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(message) != std::string_view::npos,
           "overflow has a targeted diagnostic");
  }
}
} // namespace

int main() {
  expect_literal("0x200", 512);
  expect_literal("0XfF", 255);
  expect_literal("0b1111_0000", 240);
  expect_literal("0B1010", 10);
  expect_literal("1_000_000", 1000000);

  for (const auto spelling : {"0x", "0b"})
    expect_invalid(spelling, "requires at least one");
  for (const auto spelling : {"0xg", "0x1g", "0b2", "0b10foo", "0b102"})
    expect_invalid(spelling, "invalid digit");
  for (const auto spelling : {"0x_FF", "0xFF_", "0xF__F", "0_b1"})
    expect_invalid(spelling, "separator");
  expect_parse_error("0x1_0000_0000_0000_0000",
                     "outside the unsigned 64-bit range");

  if (failures != 0)
    return 1;
  std::cout << "prefixed integer literals are lexed and parsed atomically\n";
  return 0;
}
