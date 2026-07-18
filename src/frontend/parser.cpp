#include "janus/frontend/parser.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

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

const Type *Parser::parse_type() {
  const Token type_name = expect(TokenKind::Identifier);
  if (type_name.lexeme == "int") {
    return &Type::int_type();
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
