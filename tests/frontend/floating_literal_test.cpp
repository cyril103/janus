#include "janus/ast/ast.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"

#include <bit>
#include <clocale>
#include <cstdint>
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

const janus::ast::Expression &parse_return(std::string_view spelling) {
  static janus::ast::Program program;
  const std::string source =
      "def main() : double { return " + std::string{spelling} + " }";
  janus::frontend::Parser parser{source};
  program = parser.parse_program();
  return *std::get<janus::ast::ReturnStatement>(program.functions[0].body[0])
              .expression;
}

const janus::ast::DoubleLiteralExpression &
literal_operand(const janus::ast::Expression &expression, bool negative) {
  if (!negative)
    return std::get<janus::ast::DoubleLiteralExpression>(expression.value);
  const auto &unary = std::get<janus::ast::UnaryExpression>(expression.value);
  expect(unary.operation == janus::ast::UnaryOperator::Negate,
         "negative boundary uses the negate operator");
  return std::get<janus::ast::DoubleLiteralExpression>(unary.operand->value);
}

void expect_float_bits(std::string_view spelling, std::uint32_t bits,
                       bool negative = false) {
  const auto &literal = literal_operand(parse_return(spelling), negative);
  expect(literal.is_float, "f suffix selects float");
  const float value =
      static_cast<float>(negative ? -literal.value : literal.value);
  expect(std::bit_cast<std::uint32_t>(value) == bits,
         "float literal has the expected IEEE bits");
}

void expect_double_bits(std::string_view spelling, std::uint64_t bits,
                        bool negative = false) {
  const auto &literal = literal_operand(parse_return(spelling), negative);
  expect(!literal.is_float, "unsuffixed literal selects double");
  const double value = negative ? -literal.value : literal.value;
  expect(std::bit_cast<std::uint64_t>(value) == bits,
         "double literal has the expected IEEE bits");
}

void expect_error(std::string_view spelling, janus::DiagnosticCode code,
                  std::string_view message) {
  try {
    static_cast<void>(parse_return(spelling));
    expect(false, "out-of-range floating literal must fail");
  } catch (const janus::CompileError &error) {
    expect(error.diagnostic().code == code,
           "out-of-range literal has a stable diagnostic code");
    expect(std::string_view{error.what()}.find(message) !=
               std::string_view::npos,
           "out-of-range literal has a targeted message");
  }
}
} // namespace

int main() {
  static_assert(sizeof(float) == sizeof(std::uint32_t));
  static_assert(sizeof(double) == sizeof(std::uint64_t));

  expect_float_bits("1.40129846e-45f", UINT32_C(0x00000001));
  expect_float_bits("-1.40129846e-45f", UINT32_C(0x80000001), true);
  expect_double_bits("4.9406564584124654e-324", UINT64_C(0x0000000000000001));
  expect_double_bits("-4.9406564584124654e-324", UINT64_C(0x8000000000000001),
                     true);
  expect_float_bits("1e-45f", UINT32_C(0x00000001));
  expect_double_bits("1e-320", UINT64_C(0x00000000000007e8));

  expect_error("1e-46f", janus::DiagnosticCode::ParserFloatingLiteralUnderflow,
               "float literal underflows to zero");
  expect_error("1e-325", janus::DiagnosticCode::ParserFloatingLiteralUnderflow,
               "double literal underflows to zero");
  expect_error("3.5e38f", janus::DiagnosticCode::ParserFloatingLiteralOverflow,
               "float literal overflows to infinity");
  expect_error("1e309", janus::DiagnosticCode::ParserFloatingLiteralOverflow,
               "double literal overflows to infinity");

  const char *saved_locale = std::setlocale(LC_NUMERIC, nullptr);
  const std::string saved = saved_locale == nullptr ? "" : saved_locale;
  for (const char *candidate : {"fr_FR.UTF-8", "fr_FR.utf8", "de_DE.UTF-8",
                                "de_DE.utf8", "French_France.1252"}) {
    if (std::setlocale(LC_NUMERIC, candidate) != nullptr) {
      expect_double_bits("1.5", UINT64_C(0x3ff8000000000000));
      break;
    }
  }
  if (!saved.empty())
    static_cast<void>(std::setlocale(LC_NUMERIC, saved.c_str()));

  if (failures != 0)
    return 1;
  std::cout
      << "floating literals preserve IEEE subnormals and range diagnostics\n";
  return 0;
}
