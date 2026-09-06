#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/lexer.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/frontend/unicode_identifier.hpp"
#include "janus/semantic/analyzer.hpp"

#include <clocale>
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

void expect_error(std::string_view source, std::string_view message) {
  try {
    janus::frontend::Parser parser{source};
    static_cast<void>(parser.parse_program());
    expect(false, message);
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(message) !=
               std::string_view::npos,
           message);
  }
}
} // namespace

int main() {
  using janus::frontend::Lexer;
  using janus::frontend::TokenKind;

  expect(janus::frontend::unicode::unicode_version() == "16.0.0",
         "identifier tables expose their pinned Unicode version");
  expect(janus::frontend::unicode::normalize_nfc("A\u030A\u0301") == "Ǻ" &&
             janus::frontend::unicode::normalize_nfc("각") == "각",
         "NFC composes reordered marks and Hangul algorithmically");

  Lexer lexer{"prénom Δ résultat2 cafe\u0301"};
  const auto latin = lexer.next();
  const auto greek = lexer.next();
  const auto suffixed = lexer.next();
  expect(latin.identifier() == "prénom", "Latin accents are XID");
  expect(greek.identifier() == "Δ", "non-Latin scripts are XID");
  expect(suffixed.identifier() == "résultat2", "digits may continue XID");
  const auto decomposed = lexer.next();
  expect(decomposed.kind == TokenKind::Identifier &&
             decomposed.lexeme == "cafe\u0301" &&
             decomposed.identifier() == "café",
         "tokens preserve spelling and expose an NFC identity");

  janus::frontend::Parser parser{R"(
module données.Δ
class Boîte(val valeur : int) { def lire() : int { return valeur } }
def résultat() : int {
    val prénom : string = "Ada"
    val café : int = 3
    val boîte : Boîte = new Boîte(café)
    return boîte.lire()
}
)"};
  const janus::ast::Program program = parser.parse_program();
  expect(program.module_name == "données.Δ", "Unicode module names parse");
  expect(program.classes.size() == 1 && program.functions.size() == 1 &&
             program.classes.front().name == "Boîte" &&
             !program.classes.front().constructor_fields.empty() &&
             program.classes.front().constructor_fields.front().name ==
                 "valeur" &&
             program.functions.front().name == "résultat",
         "types, members and values retain canonical Unicode identities");

  expect_error(std::string{"def main() : int { val ok : int = 1 val bad"} +
                   std::string(1, static_cast<char>(0xFF)) +
                   " : int = 2 return ok }",
               "invalid UTF-8 source");
  expect_error("def main() : int { val safe\u202Ename : int = 1 return 0 }",
               "Unicode control");
  expect_error("def main() : int { val hidden\uFE0F : int = 1 return 0 }",
               "Unicode control");

  // Canonically equivalent spellings intentionally collide after NFC.
  bool collision_rejected = false;
  try {
    janus::frontend::Parser collision_parser{
        "def main() : int { val café : int = 1 val café : int = 2 return café "
        "}"};
    const auto collision_program = collision_parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(collision_program));
  } catch (const janus::CompileError &error) {
    collision_rejected =
        std::string_view{error.what()}.find(
            "value 'café' is already declared") != std::string_view::npos;
  }
  expect(collision_rejected, "canonically equivalent declarations collide");

  janus::frontend::Parser confusables{
      "def main() : int { val a : int = 1 val а : int = 2 return a + а }"};
  const auto confusable_program = confusables.parse_program();
  expect(confusable_program.functions.size() == 1 &&
             confusable_program.functions.front().body.size() == 3,
         "visually confusable identifiers remain distinct code points");

  const char *original = std::setlocale(LC_CTYPE, nullptr);
  const std::string saved = original == nullptr ? std::string{} : original;
  std::setlocale(LC_CTYPE, "C");
  Lexer locale_independent{"élève"};
  const auto locale_token = locale_independent.next();
  expect(locale_token.identifier() == "élève",
         "XID classification is independent from LC_CTYPE");
  if (!saved.empty())
    std::setlocale(LC_CTYPE, saved.c_str());

  return failures == 0 ? 0 : 1;
}
