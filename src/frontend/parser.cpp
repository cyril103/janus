#include "janus/frontend/parser.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

namespace {

char32_t decode_character_literal(const janus::frontend::Token &token) {
  const std::string_view content =
      token.lexeme.substr(1, token.lexeme.size() - 2);

  if (content.size() == 2 && content.front() == '\\') {
    switch (content.back()) {
    case '0':
      return U'\0';
    case 'n':
      return U'\n';
    case 'r':
      return U'\r';
    case 't':
      return U'\t';
    case '\\':
      return U'\\';
    case '\'':
      return U'\'';
    default:
      throw janus::CompileError{token.location,
                                "unknown character escape sequence"};
    }
  }

  if (content.empty()) {
    throw janus::CompileError{
        token.location,
        "character literal must contain exactly one Unicode character"};
  }

  const auto byte = [&content](std::size_t index) {
    return static_cast<std::uint8_t>(content[index]);
  };

  char32_t code_point{};
  std::size_t length{};
  char32_t minimum{};
  const std::uint8_t first = byte(0);

  if (first <= 0x7F) {
    code_point = first;
    length = 1;
    minimum = 0;
  } else if (first >= 0xC2 && first <= 0xDF) {
    code_point = first & 0x1F;
    length = 2;
    minimum = 0x80;
  } else if (first >= 0xE0 && first <= 0xEF) {
    code_point = first & 0x0F;
    length = 3;
    minimum = 0x800;
  } else if (first >= 0xF0 && first <= 0xF4) {
    code_point = first & 0x07;
    length = 4;
    minimum = 0x10000;
  } else {
    throw janus::CompileError{token.location,
                              "invalid UTF-8 in character literal"};
  }

  if (content.size() != length) {
    throw janus::CompileError{
        token.location,
        "character literal must contain exactly one Unicode character"};
  }

  for (std::size_t index = 1; index < length; ++index) {
    const std::uint8_t continuation = byte(index);
    if ((continuation & 0xC0) != 0x80) {
      throw janus::CompileError{token.location,
                                "invalid UTF-8 in character literal"};
    }
    code_point = (code_point << 6) | (continuation & 0x3F);
  }

  if (code_point < minimum || code_point > 0x10FFFF ||
      (code_point >= 0xD800 && code_point <= 0xDFFF)) {
    throw janus::CompileError{token.location,
                              "invalid Unicode scalar in character literal"};
  }

  return code_point;
}

} // namespace

namespace janus::frontend {

Parser::Parser(std::string_view source)
    : lexer_{source}, current_{lexer_.next()} {}

ast::Program Parser::parse_program() {
  ast::Program program;

  while (current_.kind != TokenKind::End) {
    program.functions.push_back(parse_function_declaration());
  }

  return program;
}

ast::FunctionDeclaration Parser::parse_function_declaration() {
  const Token def = expect(TokenKind::Def);
  const Token name = expect(TokenKind::Identifier);
  static_cast<void>(expect(TokenKind::LeftParen));
  static_cast<void>(expect(TokenKind::RightParen));
  static_cast<void>(expect(TokenKind::Colon));
  const Type *return_type = parse_type();
  static_cast<void>(expect(TokenKind::LeftBrace));

  std::vector<ast::Statement> body;
  while (current_.kind != TokenKind::RightBrace) {
    if (current_.kind == TokenKind::End) {
      throw CompileError{current_.location, "expected '}', found end of file"};
    }

    body.push_back(parse_statement());
    if (current_.kind == TokenKind::Semicolon) {
      advance();
    }
  }
  static_cast<void>(expect(TokenKind::RightBrace));

  return ast::FunctionDeclaration{std::string{name.lexeme}, return_type,
                                  std::move(body), def.location};
}

ast::Statement Parser::parse_statement() {
  if (current_.kind == TokenKind::Val) {
    return parse_value_declaration();
  }
  if (current_.kind == TokenKind::Return) {
    return parse_return_statement();
  }

  throw CompileError{current_.location,
                     "expected 'val' or 'return', found " +
                         std::string{token_name(current_.kind)}};
}

ast::ValueDeclaration Parser::parse_value_declaration() {
  const Token val = expect(TokenKind::Val);
  const Token identifier = expect(TokenKind::Identifier);
  static_cast<void>(expect(TokenKind::Colon));
  const Type *declared_type = parse_type();
  static_cast<void>(expect(TokenKind::Equal));
  ast::Expression initializer = parse_expression();

  return ast::ValueDeclaration{std::string{identifier.lexeme}, declared_type,
                               false, std::move(initializer), val.location};
}

ast::ReturnStatement Parser::parse_return_statement() {
  const Token return_token = expect(TokenKind::Return);
  ast::Expression expression = parse_expression();
  return ast::ReturnStatement{std::move(expression), return_token.location};
}

ast::Expression Parser::parse_expression() {
  if (current_.kind == TokenKind::IntegerLiteral) {
    const Token literal = expect(TokenKind::IntegerLiteral);
    std::uint64_t value{};
    const auto result =
        std::from_chars(literal.lexeme.data(),
                        literal.lexeme.data() + literal.lexeme.size(), value);

    if (result.ec != std::errc{} ||
        value > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max())) {
      throw CompileError{literal.location,
                         "integer literal is outside the signed 32-bit range"};
    }

    return ast::IntegerLiteralExpression{static_cast<std::int32_t>(value),
                                         literal.location};
  }

  if (current_.kind == TokenKind::DoubleLiteral) {
    const Token literal = expect(TokenKind::DoubleLiteral);
    double value{};
    const auto result =
        std::from_chars(literal.lexeme.data(),
                        literal.lexeme.data() + literal.lexeme.size(), value);
    if (result.ec != std::errc{} || !std::isfinite(value)) {
      throw CompileError{literal.location, "invalid double literal"};
    }
    return ast::DoubleLiteralExpression{value, literal.location};
  }

  if (current_.kind == TokenKind::CharacterLiteral) {
    const Token literal = expect(TokenKind::CharacterLiteral);
    return ast::CharacterLiteralExpression{decode_character_literal(literal),
                                           literal.location};
  }

  if (current_.kind == TokenKind::True || current_.kind == TokenKind::False) {
    const bool value = current_.kind == TokenKind::True;
    const Token literal = current_;
    advance();
    return ast::BooleanLiteralExpression{value, literal.location};
  }

  throw CompileError{current_.location,
                     "expected expression, found " +
                         std::string{token_name(current_.kind)}};
}

const Type *Parser::parse_type() {
  const Token type_name = expect(TokenKind::Identifier);
  if (type_name.lexeme == "int") {
    return &Type::int_type();
  }
  if (type_name.lexeme == "double") {
    return &Type::double_type();
  }
  if (type_name.lexeme == "byte") {
    return &Type::byte_type();
  }
  if (type_name.lexeme == "char") {
    return &Type::char_type();
  }
  if (type_name.lexeme == "bool") {
    return &Type::bool_type();
  }

  throw CompileError{type_name.location,
                     "unknown type '" + std::string{type_name.lexeme} + "'"};
}

Token Parser::expect(TokenKind kind) {
  if (current_.kind != kind) {
    throw CompileError{current_.location,
                       "expected " + std::string{token_name(kind)} +
                           ", found " + std::string{token_name(current_.kind)}};
  }

  const Token token = current_;
  advance();
  return token;
}

void Parser::advance() { current_ = lexer_.next(); }

} // namespace janus::frontend
