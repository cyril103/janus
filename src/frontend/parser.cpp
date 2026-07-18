#include "janus/frontend/parser.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

namespace {

char32_t decode_utf8_scalar(std::string_view content, std::size_t &position,
                            janus::SourceLocation location,
                            std::string_view literal_kind) {
  const auto byte = [&content](std::size_t index) {
    return static_cast<std::uint8_t>(content[index]);
  };

  if (position >= content.size()) {
    throw janus::CompileError{location, "incomplete UTF-8 sequence in " +
                                            std::string{literal_kind} +
                                            " literal"};
  }

  char32_t code_point{};
  std::size_t length{};
  char32_t minimum{};
  const std::uint8_t first = byte(position);

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
    throw janus::CompileError{
        location, "invalid UTF-8 in " + std::string{literal_kind} + " literal"};
  }

  if (position + length > content.size()) {
    throw janus::CompileError{location, "incomplete UTF-8 sequence in " +
                                            std::string{literal_kind} +
                                            " literal"};
  }

  for (std::size_t index = 1; index < length; ++index) {
    const std::uint8_t continuation = byte(position + index);
    if ((continuation & 0xC0) != 0x80) {
      throw janus::CompileError{location, "invalid UTF-8 in " +
                                              std::string{literal_kind} +
                                              " literal"};
    }
    code_point = (code_point << 6) | (continuation & 0x3F);
  }

  if (code_point < minimum || code_point > 0x10FFFF ||
      (code_point >= 0xD800 && code_point <= 0xDFFF)) {
    throw janus::CompileError{location, "invalid Unicode scalar in " +
                                            std::string{literal_kind} +
                                            " literal"};
  }

  position += length;
  return code_point;
}

char decode_escape(char escaped, janus::SourceLocation location,
                   std::string_view literal_kind) {
  switch (escaped) {
  case '0':
    return '\0';
  case 'n':
    return '\n';
  case 'r':
    return '\r';
  case 't':
    return '\t';
  case '\\':
    return '\\';
  case '\'':
    return '\'';
  case '"':
    return '"';
  default:
    throw janus::CompileError{location, "unknown escape sequence in " +
                                            std::string{literal_kind} +
                                            " literal"};
  }
}

char32_t decode_character_literal(const janus::frontend::Token &token) {
  const std::string_view content =
      token.lexeme.substr(1, token.lexeme.size() - 2);

  if (content.size() == 2 && content.front() == '\\') {
    return static_cast<unsigned char>(
        decode_escape(content.back(), token.location, "character"));
  }

  if (content.empty()) {
    throw janus::CompileError{
        token.location,
        "character literal must contain exactly one Unicode character"};
  }

  std::size_t position = 0;
  const char32_t code_point =
      decode_utf8_scalar(content, position, token.location, "character");
  if (position != content.size()) {
    throw janus::CompileError{
        token.location,
        "character literal must contain exactly one Unicode character"};
  }
  return code_point;
}

std::string decode_string_literal(const janus::frontend::Token &token) {
  const std::string_view content =
      token.lexeme.substr(1, token.lexeme.size() - 2);
  std::string decoded;
  decoded.reserve(content.size());

  std::size_t position = 0;
  while (position < content.size()) {
    if (content[position] == '\\') {
      if (position + 1 >= content.size()) {
        throw janus::CompileError{token.location,
                                  "incomplete escape in string literal"};
      }
      decoded.push_back(
          decode_escape(content[position + 1], token.location, "string"));
      position += 2;
      continue;
    }

    const std::size_t scalar_start = position;
    static_cast<void>(
        decode_utf8_scalar(content, position, token.location, "string"));
    decoded.append(content.substr(scalar_start, position - scalar_start));
  }

  return decoded;
}

} // namespace

namespace janus::frontend {

Parser::Parser(std::string_view source)
    : lexer_{source}, current_{lexer_.next()} {}

ast::Program Parser::parse_program() {
  ast::Program program;

  while (current_.kind != TokenKind::End) {
    if (current_.kind == TokenKind::Class)
      program.classes.push_back(parse_class_declaration());
    else
      program.functions.push_back(parse_function_declaration());
  }

  return program;
}

ast::ClassDeclaration Parser::parse_class_declaration() {
  const Token class_token = expect(TokenKind::Class);
  const Token name = expect(TokenKind::Identifier);
  static_cast<void>(expect(TokenKind::LeftParen));
  std::vector<ast::ValueDeclaration> constructor_fields;
  if (current_.kind != TokenKind::RightParen) {
    do {
      const bool is_mutable = current_.kind == TokenKind::Var;
      const Token keyword =
          expect(is_mutable ? TokenKind::Var : TokenKind::Val);
      const Token field = expect(TokenKind::Identifier);
      static_cast<void>(expect(TokenKind::Colon));
      constructor_fields.push_back(
          ast::ValueDeclaration{std::string{field.lexeme}, parse_type(),
                                is_mutable, std::nullopt, keyword.location});
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
  }
  static_cast<void>(expect(TokenKind::RightParen));
  static_cast<void>(expect(TokenKind::LeftBrace));

  std::vector<ast::ValueDeclaration> fields;
  std::vector<ast::FunctionDeclaration> methods;
  std::optional<ast::DestructorDeclaration> destructor;
  while (current_.kind != TokenKind::RightBrace) {
    if (current_.kind == TokenKind::Val || current_.kind == TokenKind::Var)
      fields.push_back(parse_variable_declaration());
    else if (current_.kind == TokenKind::Def)
      methods.push_back(parse_function_declaration());
    else if (current_.kind == TokenKind::Destructor) {
      if (destructor.has_value())
        throw CompileError{current_.location,
                           "class cannot declare multiple destructors"};
      destructor.emplace(parse_destructor_declaration());
    } else
      throw CompileError{current_.location,
                         "expected field, method, destructor or '}'"};
    if (current_.kind == TokenKind::Semicolon)
      advance();
  }
  static_cast<void>(expect(TokenKind::RightBrace));
  return ast::ClassDeclaration{
      std::string{name.lexeme}, std::move(constructor_fields),
      std::move(fields),        std::move(methods),
      std::move(destructor),    class_token.location};
}

ast::DestructorDeclaration Parser::parse_destructor_declaration() {
  const Token destructor = expect(TokenKind::Destructor);
  return ast::DestructorDeclaration{parse_block(), destructor.location};
}

std::vector<ast::Statement> Parser::parse_block() {
  static_cast<void>(expect(TokenKind::LeftBrace));
  std::vector<ast::Statement> body;
  while (current_.kind != TokenKind::RightBrace) {
    if (current_.kind == TokenKind::End)
      throw CompileError{current_.location, "expected '}', found end of file"};
    body.push_back(parse_statement());
    if (current_.kind == TokenKind::Semicolon)
      advance();
  }
  static_cast<void>(expect(TokenKind::RightBrace));
  return body;
}

ast::FunctionDeclaration Parser::parse_function_declaration() {
  const Token def = expect(TokenKind::Def);
  const Token name = expect(TokenKind::Identifier);

  std::vector<std::string> type_parameters;
  if (current_.kind == TokenKind::LeftBracket) {
    advance();
    do {
      const Token parameter = expect(TokenKind::Identifier);
      type_parameters.emplace_back(parameter.lexeme);
      if (current_.kind != TokenKind::Comma) {
        break;
      }
      advance();
    } while (true);
    static_cast<void>(expect(TokenKind::RightBracket));
  }

  static_cast<void>(expect(TokenKind::LeftParen));

  std::vector<ast::FunctionDeclaration::Parameter> parameters;
  if (current_.kind != TokenKind::RightParen) {
    do {
      const Token parameter_name = expect(TokenKind::Identifier);
      static_cast<void>(expect(TokenKind::Colon));
      ast::TypeReference parameter_type = parse_type();
      parameters.push_back(ast::FunctionDeclaration::Parameter{
          std::string{parameter_name.lexeme}, std::move(parameter_type),
          parameter_name.location});
      if (current_.kind != TokenKind::Comma) {
        break;
      }
      advance();
    } while (true);
  }
  static_cast<void>(expect(TokenKind::RightParen));
  static_cast<void>(expect(TokenKind::Colon));
  ast::TypeReference return_type = parse_type();
  std::vector<ast::Statement> body = parse_block();

  return ast::FunctionDeclaration{
      std::string{name.lexeme}, std::move(type_parameters),
      std::move(parameters),    std::move(return_type),
      std::move(body),          def.location};
}

ast::Statement Parser::parse_statement() {
  if (current_.kind == TokenKind::Val || current_.kind == TokenKind::Var) {
    return parse_variable_declaration();
  }
  if (current_.kind == TokenKind::Identifier) {
    return parse_assignment_statement();
  }
  if (current_.kind == TokenKind::Return) {
    return parse_return_statement();
  }
  if (current_.kind == TokenKind::Delete) {
    return parse_delete_statement();
  }

  throw CompileError{current_.location,
                     "expected declaration, assignment or 'return', found " +
                         std::string{token_name(current_.kind)}};
}

ast::ValueDeclaration Parser::parse_variable_declaration() {
  const bool is_mutable = current_.kind == TokenKind::Var;
  const Token declaration =
      expect(is_mutable ? TokenKind::Var : TokenKind::Val);
  const Token identifier = expect(TokenKind::Identifier);
  static_cast<void>(expect(TokenKind::Colon));
  ast::TypeReference declared_type = parse_type();
  std::optional<ast::Expression> initializer;
  if (current_.kind == TokenKind::Equal) {
    advance();
    initializer.emplace(parse_expression());
  } else if (!is_mutable) {
    static_cast<void>(expect(TokenKind::Equal));
  }

  return ast::ValueDeclaration{std::string{identifier.lexeme},
                               std::move(declared_type), is_mutable,
                               std::move(initializer), declaration.location};
}

ast::AssignmentStatement Parser::parse_assignment_statement() {
  const Token identifier = expect(TokenKind::Identifier);
  std::string object;
  std::string name{identifier.lexeme};
  if (current_.kind == TokenKind::Dot) {
    advance();
    object = std::move(name);
    name = std::string{expect(TokenKind::Identifier).lexeme};
  }
  static_cast<void>(expect(TokenKind::Equal));
  return ast::AssignmentStatement{std::move(object), std::move(name),
                                  parse_expression(), identifier.location};
}

ast::DeleteStatement Parser::parse_delete_statement() {
  const Token delete_token = expect(TokenKind::Delete);
  return ast::DeleteStatement{parse_expression(), delete_token.location};
}

ast::ReturnStatement Parser::parse_return_statement() {
  const Token return_token = expect(TokenKind::Return);
  ast::Expression expression = parse_expression();
  return ast::ReturnStatement{std::move(expression), return_token.location};
}

ast::Expression Parser::parse_expression() {
  if (current_.kind == TokenKind::New) {
    const Token new_token = expect(TokenKind::New);
    const Token class_name = expect(TokenKind::Identifier);
    static_cast<void>(expect(TokenKind::LeftParen));
    std::vector<std::unique_ptr<ast::Expression>> arguments;
    if (current_.kind != TokenKind::RightParen) {
      do {
        arguments.push_back(
            std::make_unique<ast::Expression>(parse_expression()));
        if (current_.kind != TokenKind::Comma)
          break;
        advance();
      } while (true);
    }
    static_cast<void>(expect(TokenKind::RightParen));
    return ast::NewExpression{std::string{class_name.lexeme},
                              std::move(arguments), new_token.location};
  }

  if (current_.kind == TokenKind::Identifier) {
    const Token identifier = expect(TokenKind::Identifier);
    std::vector<ast::TypeReference> type_arguments;

    if (current_.kind == TokenKind::LeftBracket) {
      advance();
      do {
        type_arguments.push_back(parse_type());
        if (current_.kind != TokenKind::Comma) {
          break;
        }
        advance();
      } while (true);
      static_cast<void>(expect(TokenKind::RightBracket));
    }

    if (current_.kind == TokenKind::LeftParen) {
      advance();
      std::vector<std::unique_ptr<ast::Expression>> arguments;
      if (current_.kind != TokenKind::RightParen) {
        do {
          arguments.push_back(
              std::make_unique<ast::Expression>(parse_expression()));
          if (current_.kind != TokenKind::Comma) {
            break;
          }
          advance();
        } while (true);
      }
      static_cast<void>(expect(TokenKind::RightParen));
      return ast::CallExpression{std::string{identifier.lexeme},
                                 std::move(type_arguments),
                                 std::move(arguments), identifier.location};
    }

    if (!type_arguments.empty()) {
      throw CompileError{current_.location,
                         "expected '(' after generic type arguments"};
    }
    auto object = std::make_unique<ast::Expression>(ast::IdentifierExpression{
        std::string{identifier.lexeme}, identifier.location});
    if (current_.kind == TokenKind::Dot) {
      advance();
      const Token member = expect(TokenKind::Identifier);
      if (current_.kind == TokenKind::LeftParen) {
        advance();
        std::vector<std::unique_ptr<ast::Expression>> arguments;
        if (current_.kind != TokenKind::RightParen) {
          do {
            arguments.push_back(
                std::make_unique<ast::Expression>(parse_expression()));
            if (current_.kind != TokenKind::Comma)
              break;
            advance();
          } while (true);
        }
        static_cast<void>(expect(TokenKind::RightParen));
        return ast::MethodCallExpression{std::move(object),
                                         std::string{member.lexeme},
                                         std::move(arguments), member.location};
      }
      return ast::MemberAccessExpression{
          std::move(object), std::string{member.lexeme}, member.location};
    }
    return std::move(*object);
  }

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

  if (current_.kind == TokenKind::StringLiteral) {
    const Token literal = expect(TokenKind::StringLiteral);
    return ast::StringLiteralExpression{decode_string_literal(literal),
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

ast::TypeReference Parser::parse_type() {
  const Token type_name = expect(TokenKind::Identifier);
  return ast::TypeReference{std::string{type_name.lexeme}, type_name.location};
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
