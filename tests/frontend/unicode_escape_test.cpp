#include "janus/ast/ast.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <variant>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void expect_invalid(std::string_view literal, std::string_view reason) {
  const std::string source =
      "def main() : int { val value = " + std::string{literal} + " return 0 }";
  try {
    janus::frontend::Parser parser{source};
    static_cast<void>(parser.parse_program());
    expect(false, "malformed Unicode escape must be rejected");
  } catch (const janus::CompileError &error) {
    expect(error.diagnostic().code ==
               janus::DiagnosticCode::ParserInvalidUnicodeEscape,
           "malformed Unicode escape has code JPAR0004");
    expect(std::string_view{error.what()}.find(reason) !=
               std::string_view::npos,
           "malformed Unicode escape has a targeted explanation");
  }
}

void expect_multiple_character_scalars_rejected() {
  try {
    janus::frontend::Parser parser{
        R"(def main() : int { val value : char = '\u{41}B' return 0 })"};
    static_cast<void>(parser.parse_program());
    expect(false, "a character with two scalars must be rejected");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "exactly one Unicode character") != std::string_view::npos,
           "a character escape still enforces the single-scalar rule");
  }
}

} // namespace

int main() {
  janus::frontend::Parser parser{R"(
def main() : int {
  val ascii : char = '\u{41}'
  val latin : char = '\u{00E9}'
  val emoji : char = '\u{1F600}'
  val maximum : char = '\u{10FFFF}'
  val text : string = "\u{41}\u{E9}\u{1f600}"
  return 0
}
)"};
  const janus::ast::Program program = parser.parse_program();
  const auto &body = program.functions.front().body;
  const auto character = [&body](std::size_t index) {
    const auto &declaration =
        std::get<janus::ast::ValueDeclaration>(body[index]);
    return std::get<janus::ast::CharacterLiteralExpression>(
               declaration.initializer->value)
        .value;
  };
  expect(character(0) == U'A', "U+0041 escape decodes in a character");
  expect(character(1) == U'é', "U+00E9 escape decodes in a character");
  expect(character(2) == U'😀', "U+1F600 escape decodes in a character");
  expect(character(3) == 0x10FFFF,
         "the largest Unicode scalar escape is accepted");

  const auto &text_declaration =
      std::get<janus::ast::ValueDeclaration>(body[4]);
  const auto &text = std::get<janus::ast::StringLiteralExpression>(
                         text_declaration.initializer->value)
                         .value;
  expect(text == "Aé😀", "Unicode escapes are encoded as UTF-8 in strings");

  expect_invalid(R"('\u{}')", "at least one hexadecimal digit");
  expect_invalid(R"('\u41')", "expected '{'");
  expect_invalid(R"('\u{41')", "missing closing '}'");
  expect_invalid(R"('\u{GG}')", "expected a hexadecimal digit");
  expect_invalid(R"('\u{0000041}')", "at most six hexadecimal digits");
  expect_invalid(R"('\u{D800}')", "not a Unicode scalar");
  expect_invalid(R"('\u{110000}')", "not a Unicode scalar");
  expect_multiple_character_scalars_rejected();

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout
      << "Unicode scalar escapes decode strictly in char and string literals\n";
  return 0;
}
