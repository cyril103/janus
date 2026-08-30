#include "janus/frontend/parser.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_set>

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

namespace {

std::optional<std::uint64_t> parse_integer_literal(std::string_view spelling) {
  int base = 10;
  std::size_t start = 0;
  if (spelling.size() >= 2 && spelling[0] == '0') {
    if (spelling[1] == 'x' || spelling[1] == 'X') {
      base = 16;
      start = 2;
    } else if (spelling[1] == 'b' || spelling[1] == 'B') {
      base = 2;
      start = 2;
    }
  }
  std::string digits;
  digits.reserve(spelling.size() - start);
  for (std::size_t index = start; index < spelling.size(); ++index)
    if (spelling[index] != '_')
      digits.push_back(spelling[index]);
  std::uint64_t value{};
  const auto result = std::from_chars(
      digits.data(), digits.data() + digits.size(), value, base);
  if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
    return std::nullopt;
  return value;
}

} // namespace

Parser::Parser(std::string_view source)
    : lexer_{source}, current_{lexer_.next()}, source_{source} {}

ast::Program Parser::parse_program() {
  ast::Program program;
  std::vector<Diagnostic> diagnostics;
  std::string documentation = take_documentation();

  if (current_.kind == TokenKind::Module) {
    program.documentation = std::move(documentation);
    documentation.clear();
    advance();
    program.module_name = parse_qualified_name();
    if (current_.kind == TokenKind::Semicolon)
      advance();
  }
  while (current_.kind == TokenKind::Import) {
    ast::ImportDeclaration import = parse_import_declaration();
    import.importing_module = program.module_name;
    program.imports.push_back(std::move(import));
    if (current_.kind == TokenKind::Semicolon)
      advance();
  }

  while (current_.kind != TokenKind::End) {
    try {
      if (documentation.empty())
        documentation = take_documentation();
      if (current_.kind == TokenKind::End)
        break;
      bool is_private = false;
      bool is_internal = false;
      if (current_.kind == TokenKind::Private) {
        is_private = true;
        advance();
      } else if (current_.kind == TokenKind::Internal) {
        is_internal = true;
        advance();
      }
      if (is_private && current_.kind != TokenKind::Const &&
          current_.kind != TokenKind::Val && current_.kind != TokenKind::Var &&
          current_.kind != TokenKind::Def &&
          current_.kind != TokenKind::Tailrec &&
          current_.kind != TokenKind::Extern &&
          current_.kind != TokenKind::Class &&
          current_.kind != TokenKind::Struct &&
          current_.kind != TokenKind::Trait &&
          current_.kind != TokenKind::Enum &&
          !(current_.kind == TokenKind::Identifier &&
            current_.lexeme == "extend"))
        throw CompileError{
            current_.location,
            "expected a top-level declaration after 'private', found " +
                std::string{token_name(current_.kind)}};
      if (is_internal && current_.kind != TokenKind::Const)
        throw CompileError{
            current_.location,
            "'internal' can only modify class fields and methods"};

      if (current_.kind == TokenKind::Trait) {
        ast::TraitDeclaration declaration = parse_trait_declaration();
        declaration.is_private = is_private;
        declaration.module_name = program.module_name;
        declaration.documentation = std::move(documentation);
        program.traits.push_back(std::move(declaration));
      } else if (current_.kind == TokenKind::Identifier &&
                 current_.lexeme == "extend") {
        ast::ExtensionDeclaration declaration = parse_extension_declaration();
        declaration.is_private = is_private;
        declaration.module_name = program.module_name;
        declaration.documentation = std::move(documentation);
        for (ast::FunctionDeclaration &method : declaration.methods)
          method.module_name = program.module_name;
        program.extensions.push_back(std::move(declaration));
      } else if (current_.kind == TokenKind::Enum) {
        ast::EnumDeclaration declaration = parse_enum_declaration();
        declaration.is_private = is_private;
        declaration.module_name = program.module_name;
        declaration.documentation = std::move(documentation);
        program.enums.push_back(std::move(declaration));
      } else if (current_.kind == TokenKind::Class ||
                 current_.kind == TokenKind::Struct) {
        ast::ClassDeclaration declaration = parse_class_declaration();
        declaration.is_private = is_private;
        declaration.module_name = program.module_name;
        declaration.documentation = std::move(documentation);
        program.classes.push_back(std::move(declaration));
      } else if (current_.kind == TokenKind::StaticAssert) {
        if (is_private)
          throw CompileError{current_.location,
                             "staticAssert cannot be private"};
        auto assertion = parse_static_assertion();
        assertion.module_name = program.module_name;
        program.static_assertions.push_back(std::move(assertion));
      } else if (current_.kind == TokenKind::Const) {
        const SourceLocation location = current_.location;
        advance();
        if (current_.kind == TokenKind::Def ||
            current_.kind == TokenKind::Tailrec) {
          ast::FunctionDeclaration declaration =
              parse_function_declaration(true);
          declaration.is_private = is_private;
          declaration.is_internal = is_internal;
          declaration.module_name = program.module_name;
          declaration.documentation = std::move(documentation);
          program.functions.push_back(std::move(declaration));
        } else {
          ast::ValueDeclaration declaration =
              parse_variable_declaration(true, location);
          if (!declaration.declared_type.has_value())
            throw CompileError{declaration.location,
                               "constant '" + declaration.name +
                                   "' requires an explicit type annotation"};
          declaration.is_private = is_private;
          declaration.is_internal = is_internal;
          declaration.documentation = std::move(documentation);
          program.globals.push_back(ast::GlobalDeclaration{
              std::move(declaration), program.module_name});
        }
      } else if (current_.kind == TokenKind::Val ||
                 current_.kind == TokenKind::Var) {
        ast::ValueDeclaration declaration = parse_variable_declaration();
        if (!declaration.declared_type.has_value())
          throw CompileError{declaration.location,
                             "global value '" + declaration.name +
                                 "' requires an explicit type annotation"};
        declaration.is_private = is_private;
        declaration.is_internal = is_internal;
        declaration.documentation = std::move(documentation);
        program.globals.push_back(ast::GlobalDeclaration{std::move(declaration),
                                                         program.module_name});
      } else {
        ast::FunctionDeclaration declaration = parse_function_declaration();
        declaration.is_private = is_private;
        declaration.is_internal = is_internal;
        declaration.module_name = program.module_name;
        declaration.documentation = std::move(documentation);
        program.functions.push_back(std::move(declaration));
      }
      documentation.clear();
    } catch (const CompileError &error) {
      documentation.clear();
      diagnostics.insert(diagnostics.end(), error.diagnostics().begin(),
                         error.diagnostics().end());
      synchronize_top_level();
    }
  }

  if (!diagnostics.empty())
    throw CompileError{std::move(diagnostics)};
  return program;
}

void Parser::synchronize_top_level() {
  std::size_t brace_depth = 0;
  while (current_.kind != TokenKind::End) {
    if (current_.kind == TokenKind::RightBrace) {
      if (brace_depth == 0) {
        advance();
        return;
      }
      --brace_depth;
    } else if (current_.kind == TokenKind::LeftBrace) {
      ++brace_depth;
    } else if (brace_depth == 0 && (current_.kind == TokenKind::Private ||
                                    current_.kind == TokenKind::Trait ||
                                    (current_.kind == TokenKind::Identifier &&
                                     current_.lexeme == "extend") ||
                                    current_.kind == TokenKind::Enum ||
                                    current_.kind == TokenKind::Class ||
                                    current_.kind == TokenKind::Struct ||
                                    current_.kind == TokenKind::Const ||
                                    current_.kind == TokenKind::StaticAssert ||
                                    current_.kind == TokenKind::Val ||
                                    current_.kind == TokenKind::Var ||
                                    current_.kind == TokenKind::Def ||
                                    current_.kind == TokenKind::Tailrec ||
                                    current_.kind == TokenKind::Extern)) {
      return;
    }
    advance();
  }
}

ast::TraitDeclaration Parser::parse_trait_declaration() {
  const Token trait_token = expect(TokenKind::Trait);
  const Token name = expect(TokenKind::Identifier);
  std::vector<std::string> type_parameters;
  std::vector<ast::TypeConstraint> type_constraints;
  if (current_.kind == TokenKind::LeftBracket) {
    advance();
    do {
      const Token parameter = expect(TokenKind::Identifier);
      type_parameters.emplace_back(parameter.lexeme);
      if (current_.kind == TokenKind::Less) {
        advance();
        static_cast<void>(expect(TokenKind::Colon));
        do {
          type_constraints.push_back(ast::TypeConstraint{
              std::string{parameter.lexeme}, parse_type(), parameter.location});
          if (current_.kind != TokenKind::Ampersand)
            break;
          advance();
        } while (true);
      }
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
    static_cast<void>(expect(TokenKind::RightBracket));
  }
  static_cast<void>(expect(TokenKind::LeftBrace));
  std::vector<ast::FunctionDeclaration> methods;
  std::vector<ast::AssociatedTypeDeclaration> associated_types;
  while (current_.kind != TokenKind::RightBrace) {
    std::string documentation = take_documentation();
    if (current_.kind == TokenKind::Type) {
      const Token keyword = expect(TokenKind::Type);
      const Token associated_name = expect(TokenKind::Identifier);
      if (current_.kind == TokenKind::Equal)
        throw CompileError{current_.location,
                           "a trait associated type cannot have a definition"};
      associated_types.push_back(ast::AssociatedTypeDeclaration{
          std::string{associated_name.lexeme}, std::nullopt, keyword.location,
          std::move(documentation)});
      if (current_.kind == TokenKind::Semicolon)
        advance();
      continue;
    }
    ast::FunctionDeclaration method = parse_trait_method();
    method.documentation = std::move(documentation);
    methods.push_back(std::move(method));
    if (current_.kind == TokenKind::Semicolon)
      advance();
  }
  static_cast<void>(expect(TokenKind::RightBrace));
  ast::TraitDeclaration declaration{std::string{name.lexeme},
                                    std::move(type_parameters),
                                    std::move(methods),
                                    trait_token.location,
                                    std::move(type_constraints),
                                    false,
                                    std::nullopt,
                                    {},
                                    {}};
  declaration.associated_types = std::move(associated_types);
  return declaration;
}

ast::FunctionDeclaration Parser::parse_trait_method() {
  const bool is_borrowing = current_.kind == TokenKind::Borrow;
  const bool is_consuming = current_.kind == TokenKind::Consume;
  if (is_borrowing || is_consuming)
    advance();
  const Token def = expect(TokenKind::Def);
  const Token name = expect(TokenKind::Identifier);
  std::vector<std::string> type_parameters;
  std::vector<ast::TypeConstraint> type_constraints;
  if (current_.kind == TokenKind::LeftBracket) {
    advance();
    do {
      const Token parameter = expect(TokenKind::Identifier);
      type_parameters.emplace_back(parameter.lexeme);
      if (current_.kind == TokenKind::Less) {
        advance();
        static_cast<void>(expect(TokenKind::Colon));
        do {
          type_constraints.push_back(ast::TypeConstraint{
              std::string{parameter.lexeme}, parse_type(), parameter.location});
          if (current_.kind != TokenKind::Ampersand)
            break;
          advance();
        } while (true);
      }
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
    static_cast<void>(expect(TokenKind::RightBracket));
  }
  static_cast<void>(expect(TokenKind::LeftParen));
  std::vector<ast::FunctionDeclaration::Parameter> parameters;
  if (current_.kind != TokenKind::RightParen) {
    do {
      const bool parameter_is_scoped =
          current_.kind == TokenKind::Identifier &&
          current_.lexeme == "scoped";
      if (parameter_is_scoped)
        advance();
      const bool parameter_is_borrowed = current_.kind == TokenKind::Borrow;
      if (parameter_is_borrowed)
        advance();
      const bool parameter_is_mutably_borrowed =
          parameter_is_borrowed && current_.kind == TokenKind::Var;
      if (parameter_is_mutably_borrowed)
        advance();
      const Token parameter = expect(TokenKind::Identifier);
      static_cast<void>(expect(TokenKind::Colon));
      parameters.push_back(ast::FunctionDeclaration::Parameter{
          std::string{parameter.lexeme}, parse_type(), parameter.location,
          parameter_is_mutably_borrowed
              ? ast::ParameterOwnership::BorrowMutable
              : (parameter_is_borrowed ? ast::ParameterOwnership::Borrow
                                       : ast::ParameterOwnership::Unspecified),
          parameter_is_scoped});
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
  }
  static_cast<void>(expect(TokenKind::RightParen));
  static_cast<void>(expect(TokenKind::Colon));
  ast::ReturnOwnership return_ownership = ast::ReturnOwnership::Unspecified;
  if (current_.kind == TokenKind::Borrow) {
    advance();
    return_ownership = current_.kind == TokenKind::Var
                           ? ast::ReturnOwnership::BorrowMutable
                           : ast::ReturnOwnership::Borrow;
    if (current_.kind == TokenKind::Var)
      advance();
  }
  ast::TypeReference return_type = parse_type();
  if (current_.kind == TokenKind::Identifier && current_.lexeme == "where") {
    advance();
    do {
      const Token parameter = expect(TokenKind::Identifier);
      static_cast<void>(expect(TokenKind::Less));
      static_cast<void>(expect(TokenKind::Colon));
      do {
        type_constraints.push_back(ast::TypeConstraint{
            std::string{parameter.lexeme}, parse_type(), parameter.location});
        if (current_.kind != TokenKind::Ampersand)
          break;
        advance();
      } while (true);
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
  }
  ast::FunctionDeclaration declaration{std::string{name.lexeme},
                                       std::move(type_parameters),
                                       std::move(parameters),
                                       std::move(return_type),
                                       {},
                                       def.location,
                                       false,
                                       is_consuming,
                                       std::move(type_constraints),
                                       false,
                                       std::nullopt,
                                       false,
                                       std::nullopt,
                                       false,
                                       {},
                                       ast::ReturnOwnership::Unspecified,
                                       false,
                                       false,
                                       false,
                                       {},
                                       {},
                                       0};
  declaration.is_borrowing = is_borrowing;
  declaration.return_ownership = return_ownership;
  return declaration;
}

ast::EnumDeclaration Parser::parse_enum_declaration() {
  const Token enum_token = expect(TokenKind::Enum);
  const Token name = expect(TokenKind::Identifier);
  std::vector<std::string> type_parameters;
  if (current_.kind == TokenKind::LeftBracket) {
    advance();
    do {
      type_parameters.emplace_back(expect(TokenKind::Identifier).lexeme);
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
    static_cast<void>(expect(TokenKind::RightBracket));
  }
  std::vector<ast::TypeReference> implemented_traits;
  if (current_.kind == TokenKind::Extends) {
    advance();
    do {
      implemented_traits.push_back(parse_type());
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
  }
  std::vector<ast::Derivation> derivations = parse_derivations();
  static_cast<void>(expect(TokenKind::LeftBrace));

  std::vector<ast::AssociatedTypeDeclaration> associated_types;
  std::string pending_case_documentation;
  while (current_.kind == TokenKind::Type ||
         current_.kind == TokenKind::DocumentationComment) {
    std::string documentation = take_documentation();
    if (current_.kind != TokenKind::Type) {
      pending_case_documentation = std::move(documentation);
      break;
    }
    const Token keyword = expect(TokenKind::Type);
    const Token associated_name = expect(TokenKind::Identifier);
    static_cast<void>(expect(TokenKind::Equal));
    associated_types.push_back(ast::AssociatedTypeDeclaration{
        std::string{associated_name.lexeme}, parse_type(), keyword.location,
        std::move(documentation)});
    if (current_.kind == TokenKind::Comma ||
        current_.kind == TokenKind::Semicolon)
      advance();
  }
  std::vector<ast::EnumDeclaration::Case> cases;
  std::int64_t next_value = 0;
  while (current_.kind != TokenKind::RightBrace) {
    std::string documentation =
        pending_case_documentation.empty()
            ? take_documentation()
            : std::exchange(pending_case_documentation, {});
    const Token case_name = expect(TokenKind::Identifier);
    std::vector<ast::TypeReference> payload_types;
    if (current_.kind == TokenKind::LeftParen) {
      advance();
      if (current_.kind != TokenKind::RightParen) {
        do {
          payload_types.push_back(parse_type());
          if (current_.kind != TokenKind::Comma)
            break;
          advance();
        } while (true);
      }
      static_cast<void>(expect(TokenKind::RightParen));
    }
    if (next_value > static_cast<std::int64_t>(
                         std::numeric_limits<std::int32_t>::max()) &&
        current_.kind != TokenKind::Equal)
      throw CompileError{case_name.location,
                         "enum discriminant is outside the signed 32-bit "
                         "range; specify an explicit value"};
    std::int64_t value = next_value;
    if (current_.kind == TokenKind::Equal) {
      advance();
      bool negative = false;
      if (current_.kind == TokenKind::Minus) {
        negative = true;
        advance();
      }
      const Token literal = expect(TokenKind::IntegerLiteral);
      const auto parsed_magnitude = parse_integer_literal(literal.lexeme);
      const std::uint64_t magnitude = parsed_magnitude.value_or(0);
      const std::uint64_t limit =
          negative ? static_cast<std::uint64_t>(
                         std::numeric_limits<std::int32_t>::max()) +
                         1
                   : static_cast<std::uint64_t>(
                         std::numeric_limits<std::int32_t>::max());
      if (!parsed_magnitude || magnitude > limit)
        throw CompileError{
            literal.location,
            "enum discriminant is outside the signed 32-bit range"};
      value = negative ? -static_cast<std::int64_t>(magnitude)
                       : static_cast<std::int64_t>(magnitude);
    }
    cases.push_back(ast::EnumDeclaration::Case{
        std::string{case_name.lexeme}, static_cast<std::int32_t>(value),
        std::move(payload_types), case_name.location,
        std::move(documentation)});

    if (value == std::numeric_limits<std::int32_t>::max()) {
      next_value =
          static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) +
          1;
    } else {
      next_value = value + 1;
    }
    if (current_.kind == TokenKind::Comma ||
        current_.kind == TokenKind::Semicolon) {
      advance();
      continue;
    }
    if (current_.kind != TokenKind::RightBrace)
      throw CompileError{current_.location,
                         "expected ',' or '}' after enum case"};
  }
  static_cast<void>(expect(TokenKind::RightBrace));
  if (cases.empty())
    throw CompileError{enum_token.location,
                       "enum '" + std::string{name.lexeme} +
                           "' must declare at least one case"};
  ast::EnumDeclaration declaration{std::string{name.lexeme},
                                   std::move(type_parameters),
                                   std::move(implemented_traits),
                                   std::move(cases),
                                   enum_token.location,
                                   false,
                                   std::nullopt,
                                   std::move(derivations),
                                   {},
                                   std::move(associated_types)};
  return declaration;
}

std::string Parser::parse_qualified_name() {
  std::string name{expect(TokenKind::Identifier).lexeme};
  while (current_.kind == TokenKind::Dot) {
    advance();
    name += '.';
    name += expect(TokenKind::Identifier).lexeme;
  }
  return name;
}

ast::ImportDeclaration Parser::parse_import_declaration() {
  const Token import_token = expect(TokenKind::Import);
  std::string module{expect(TokenKind::Identifier).lexeme};
  std::vector<ast::ImportDeclaration::Symbol> symbols;

  while (current_.kind == TokenKind::Dot) {
    advance();
    if (current_.kind == TokenKind::LeftBrace) {
      advance();
      if (current_.kind == TokenKind::RightBrace)
        throw CompileError{current_.location,
                           "a selective import must name at least one symbol"};
      do {
        const Token name = expect(TokenKind::Identifier);
        std::optional<std::string> alias;
        if (current_.kind == TokenKind::As) {
          advance();
          alias = std::string{expect(TokenKind::Identifier).lexeme};
        }
        symbols.push_back(ast::ImportDeclaration::Symbol{
            std::string{name.lexeme}, std::move(alias), name.location});
        if (current_.kind != TokenKind::Comma)
          break;
        advance();
      } while (true);
      static_cast<void>(expect(TokenKind::RightBrace));
      break;
    }
    module += '.';
    module += expect(TokenKind::Identifier).lexeme;
  }

  std::optional<std::string> alias;
  if (current_.kind == TokenKind::As) {
    if (!symbols.empty())
      throw CompileError{current_.location,
                         "a selective import cannot also alias its module"};
    advance();
    alias = std::string{expect(TokenKind::Identifier).lexeme};
  }
  return ast::ImportDeclaration{std::move(module),
                                std::move(alias),
                                std::move(symbols),
                                import_token.location,
                                {}};
}

std::string Parser::take_documentation() {
  std::string documentation;
  while (current_.kind == TokenKind::DocumentationComment) {
    std::string_view line = current_.lexeme;
    if (!line.empty() && line.front() == ' ')
      line.remove_prefix(1);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (!documentation.empty())
      documentation += '\n';
    documentation += line;
    advance();
  }
  return documentation;
}

std::vector<ast::Derivation> Parser::parse_derivations() {
  std::vector<ast::Derivation> derivations;
  if (current_.kind != TokenKind::Derives)
    return derivations;

  const Token derives = expect(TokenKind::Derives);
  if (current_.kind != TokenKind::Identifier)
    throw CompileError{derives.location,
                       "expected a derivation name after 'derives'"};

  do {
    const Token name = expect(TokenKind::Identifier);
    std::optional<ast::DerivationKind> kind;
    if (name.lexeme == "Copy")
      kind = ast::DerivationKind::Copy;
    else if (name.lexeme == "Equality")
      kind = ast::DerivationKind::Equality;
    else if (name.lexeme == "Hashing")
      kind = ast::DerivationKind::Hashing;
    else if (name.lexeme == "Debug")
      kind = ast::DerivationKind::Debug;
    else
      throw CompileError{name.location,
                         "unknown derivation '" + std::string{name.lexeme} +
                             "'; expected Copy, Equality, Hashing or Debug"};

    for (const ast::Derivation &derivation : derivations)
      if (derivation.kind == *kind)
        throw CompileError{name.location, "derivation '" +
                                              std::string{name.lexeme} +
                                              "' is requested more than once"};
    derivations.push_back(ast::Derivation{*kind, name.location});

    if (current_.kind != TokenKind::Comma)
      break;
    advance();
  } while (true);
  return derivations;
}

ast::ClassDeclaration Parser::parse_class_declaration() {
  const bool is_value_type = current_.kind == TokenKind::Struct;
  const Token class_token =
      expect(is_value_type ? TokenKind::Struct : TokenKind::Class);
  const Token name = expect(TokenKind::Identifier);

  std::vector<std::string> type_parameters;
  std::vector<ast::TypeConstraint> type_constraints;
  if (current_.kind == TokenKind::LeftBracket) {
    advance();
    do {
      const Token parameter = expect(TokenKind::Identifier);
      type_parameters.emplace_back(parameter.lexeme);
      if (current_.kind == TokenKind::Less) {
        advance();
        static_cast<void>(expect(TokenKind::Colon));
        do {
          type_constraints.push_back(ast::TypeConstraint{
              std::string{parameter.lexeme}, parse_type(), parameter.location});
          if (current_.kind != TokenKind::Ampersand)
            break;
          advance();
        } while (true);
      }
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
    static_cast<void>(expect(TokenKind::RightBracket));
  }

  const bool is_constructor_internal = current_.kind == TokenKind::Internal;
  if (is_constructor_internal) {
    if (is_value_type)
      throw CompileError{current_.location,
                         "struct constructors cannot be internal"};
    advance();
  }

  static_cast<void>(expect(TokenKind::LeftParen));
  std::vector<ast::FunctionDeclaration::Parameter> constructor_parameters;
  std::vector<ast::ValueDeclaration> constructor_fields;
  bool parsed_field = false;
  if (current_.kind != TokenKind::RightParen) {
    do {
      std::string documentation = take_documentation();
      const bool is_private = current_.kind == TokenKind::Private;
      const bool is_internal = current_.kind == TokenKind::Internal;
      if (is_private || is_internal)
        advance();
      if ((is_private && current_.kind == TokenKind::Internal) ||
          (is_internal && current_.kind == TokenKind::Private))
        throw CompileError{
            current_.location,
            "a class member cannot be both private and internal"};
      const bool is_borrowed = current_.kind == TokenKind::Borrow;
      if (is_borrowed)
        advance();
      if (current_.kind == TokenKind::Val || current_.kind == TokenKind::Var) {
        if (is_borrowed && is_value_type)
          throw CompileError{current_.location,
                             "struct fields cannot be borrowed"};
        parsed_field = true;
        const bool is_mutable = current_.kind == TokenKind::Var;
        const Token keyword =
            expect(is_mutable ? TokenKind::Var : TokenKind::Val);
        const Token field = expect(TokenKind::Identifier);
        static_cast<void>(expect(TokenKind::Colon));
        ast::ValueDeclaration declaration{
            std::string{field.lexeme}, parse_type(), is_mutable, std::nullopt,
            keyword.location,          false,        false,      {}};
        declaration.is_private = is_private;
        declaration.is_internal = is_internal;
        declaration.documentation = std::move(documentation);
        declaration.is_borrowed = is_borrowed;
        constructor_fields.push_back(std::move(declaration));
      } else {
        if (is_private || is_internal || is_borrowed)
          throw CompileError{
              current_.location,
              "a non-field constructor parameter cannot have visibility or "
              "borrow ownership"};
        if (parsed_field)
          throw CompileError{
              current_.location,
              "non-field constructor parameters must precede val/var fields"};
        const Token parameter = expect(TokenKind::Identifier);
        static_cast<void>(expect(TokenKind::Colon));
        constructor_parameters.push_back(ast::FunctionDeclaration::Parameter{
            std::string{parameter.lexeme}, parse_type(), parameter.location});
      }
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
  }
  static_cast<void>(expect(TokenKind::RightParen));
  std::vector<ast::TypeReference> implemented_traits;
  if (current_.kind == TokenKind::Extends) {
    advance();
    do {
      implemented_traits.push_back(parse_type());
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
  }
  std::vector<ast::Derivation> derivations = parse_derivations();
  static_cast<void>(expect(TokenKind::LeftBrace));

  std::vector<ast::ValueDeclaration> fields;
  std::vector<ast::FunctionDeclaration> methods;
  std::vector<ast::AssociatedTypeDeclaration> associated_types;
  std::optional<ast::DestructorDeclaration> destructor;
  while (current_.kind != TokenKind::RightBrace) {
    std::string documentation = take_documentation();
    const bool is_private = current_.kind == TokenKind::Private;
    const bool is_internal = current_.kind == TokenKind::Internal;
    if (is_private || is_internal)
      advance();
    if ((is_private && current_.kind == TokenKind::Internal) ||
        (is_internal && current_.kind == TokenKind::Private))
      throw CompileError{current_.location,
                         "a class member cannot be both private and internal"};
    const bool is_borrowing = current_.kind == TokenKind::Borrow;
    const bool is_consuming = current_.kind == TokenKind::Consume;
    if (is_borrowing || is_consuming)
      advance();
    const bool is_tailrec = current_.kind == TokenKind::Tailrec;
    if (current_.kind == TokenKind::Type) {
      if (is_private || is_internal || is_borrowing || is_consuming)
        throw CompileError{
            current_.location,
            "an associated type definition cannot have member modifiers"};
      const Token keyword = expect(TokenKind::Type);
      const Token associated_name = expect(TokenKind::Identifier);
      static_cast<void>(expect(TokenKind::Equal));
      associated_types.push_back(ast::AssociatedTypeDeclaration{
          std::string{associated_name.lexeme}, parse_type(), keyword.location,
          std::move(documentation)});
    } else if (current_.kind == TokenKind::Val || current_.kind == TokenKind::Var) {
      if (is_borrowing || is_consuming)
        throw CompileError{current_.location,
                           "borrow and consume can only modify a method"};
      ast::ValueDeclaration field = parse_variable_declaration();
      if (!field.declared_type.has_value())
        throw CompileError{field.location,
                           "field '" + field.name +
                               "' requires an explicit type annotation"};
      field.is_private = is_private;
      field.is_internal = is_internal;
      field.documentation = std::move(documentation);
      fields.push_back(std::move(field));
    } else if (current_.kind == TokenKind::Def || is_tailrec) {
      ast::FunctionDeclaration method = parse_function_declaration();
      method.is_private = is_private;
      method.is_internal = is_internal;
      method.is_consuming = is_consuming;
      method.is_borrowing = is_borrowing;
      method.is_tailrec = is_tailrec;
      method.documentation = std::move(documentation);
      methods.push_back(std::move(method));
    } else if (current_.kind == TokenKind::Destructor) {
      if (is_private || is_internal || is_borrowing || is_consuming)
        throw CompileError{current_.location,
                           "destructor cannot have method modifiers"};
      if (destructor.has_value())
        throw CompileError{current_.location,
                           "class cannot declare multiple destructors"};
      destructor.emplace(parse_destructor_declaration());
      destructor->documentation = std::move(documentation);
    } else
      throw CompileError{current_.location,
                         "expected field, method, destructor or '}'"};
    if (current_.kind == TokenKind::Semicolon)
      advance();
  }
  static_cast<void>(expect(TokenKind::RightBrace));
  ast::ClassDeclaration declaration{std::string{name.lexeme},
                                    std::move(type_parameters),
                                    std::move(implemented_traits),
                                    std::move(constructor_parameters),
                                    std::move(constructor_fields),
                                    std::move(fields),
                                    std::move(methods),
                                    std::move(destructor),
                                    class_token.location,
                                    std::move(type_constraints),
                                    false,
                                    false,
                                    is_constructor_internal,
                                    std::nullopt,
                                    std::move(derivations),
                                    {},
                                    {}};
  declaration.is_value_type = is_value_type;
  declaration.associated_types = std::move(associated_types);
  return declaration;
}

ast::ExtensionDeclaration Parser::parse_extension_declaration() {
  if (current_.kind != TokenKind::Identifier || current_.lexeme != "extend")
    throw CompileError{current_.location, "expected 'extend'"};
  const Token extension_token = current_;
  advance();
  std::vector<std::string> type_parameters;
  if (current_.kind == TokenKind::LeftBracket) {
    advance();
    do {
      const Token parameter = expect(TokenKind::Identifier);
      if (std::find(type_parameters.begin(), type_parameters.end(),
                    parameter.lexeme) != type_parameters.end())
        throw CompileError{parameter.location,
                           "extension type parameter '" +
                               std::string{parameter.lexeme} +
                               "' is already declared"};
      type_parameters.emplace_back(parameter.lexeme);
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
    static_cast<void>(expect(TokenKind::RightBracket));
  }
  ast::TypeReference target_type = parse_type();
  static_cast<void>(expect(TokenKind::LeftBrace));

  std::vector<ast::FunctionDeclaration> methods;
  std::vector<ast::ParameterOwnership> receiver_ownerships;
  std::unordered_set<std::string> method_names;
  while (current_.kind != TokenKind::RightBrace) {
    std::string documentation = take_documentation();
    ast::ParameterOwnership receiver_ownership;
    bool borrowing = false;
    bool consuming = false;
    if (current_.kind == TokenKind::Borrow) {
      borrowing = true;
      receiver_ownership = ast::ParameterOwnership::Borrow;
      advance();
      if (current_.kind == TokenKind::Var) {
        receiver_ownership = ast::ParameterOwnership::BorrowMutable;
        advance();
      }
    } else if (current_.kind == TokenKind::Consume) {
      consuming = true;
      receiver_ownership = ast::ParameterOwnership::Consume;
      advance();
    } else {
      throw CompileError{
          current_.location,
          "extension method requires 'borrow', 'borrow var', or 'consume'"};
    }
    ast::FunctionDeclaration method = parse_function_declaration();
    if (method.is_external || method.is_tailrec)
      throw CompileError{
          method.location,
          "extension methods cannot be external or tail-recursive"};
    if (!method_names.insert(method.name).second)
      throw CompileError{method.location,
                         "extension method '" + method.name +
                             "' is already declared in this block"};
    method.is_borrowing = borrowing;
    method.is_consuming = consuming;
    method.documentation = std::move(documentation);
    methods.push_back(std::move(method));
    receiver_ownerships.push_back(receiver_ownership);
    if (current_.kind == TokenKind::Semicolon)
      advance();
  }
  static_cast<void>(expect(TokenKind::RightBrace));
  return ast::ExtensionDeclaration{std::move(type_parameters),
                                   std::move(target_type),
                                   std::move(methods),
                                   std::move(receiver_ownerships),
                                   extension_token.location,
                                   false,
                                   std::nullopt,
                                   {}};
}

ast::DestructorDeclaration Parser::parse_destructor_declaration() {
  const Token destructor = expect(TokenKind::Destructor);
  return ast::DestructorDeclaration{parse_block(), destructor.location, {}};
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

ast::FunctionDeclaration Parser::parse_function_declaration(bool is_constant) {
  const bool is_tailrec = current_.kind == TokenKind::Tailrec;
  if (is_tailrec)
    advance();
  const bool is_external = current_.kind == TokenKind::Extern;
  if (is_tailrec && is_external)
    throw CompileError{current_.location,
                       "tailrec must directly precede a Janus function definition"};
  const SourceLocation declaration_location = current_.location;
  std::optional<std::string> external_symbol;
  if (is_external)
    advance();
  if (is_external && current_.kind == TokenKind::LeftParen) {
    advance();
    const Token symbol = expect(TokenKind::StringLiteral);
    external_symbol = decode_string_literal(symbol);
    static_cast<void>(expect(TokenKind::RightParen));
  }
  const Token def = expect(TokenKind::Def);
  const Token name = expect(TokenKind::Identifier);

  std::vector<std::string> type_parameters;
  std::vector<ast::TypeConstraint> type_constraints;
  if (current_.kind == TokenKind::LeftBracket) {
    advance();
    do {
      const Token parameter = expect(TokenKind::Identifier);
      type_parameters.emplace_back(parameter.lexeme);
      if (current_.kind == TokenKind::Less) {
        advance();
        static_cast<void>(expect(TokenKind::Colon));
        do {
          type_constraints.push_back(ast::TypeConstraint{
              std::string{parameter.lexeme}, parse_type(), parameter.location});
          if (current_.kind != TokenKind::Ampersand)
            break;
          advance();
        } while (true);
      }
      if (current_.kind != TokenKind::Comma) {
        break;
      }
      advance();
    } while (true);
    static_cast<void>(expect(TokenKind::RightBracket));
  }

  static_cast<void>(expect(TokenKind::LeftParen));

  std::vector<ast::FunctionDeclaration::Parameter> parameters;
  bool is_variadic = false;
  if (current_.kind != TokenKind::RightParen) {
    do {
      if (current_.kind == TokenKind::Ellipsis) {
        is_variadic = true;
        advance();
        break;
      }
      const bool is_scoped = current_.kind == TokenKind::Identifier &&
                             current_.lexeme == "scoped";
      if (is_scoped)
        advance();
      ast::ParameterOwnership ownership = ast::ParameterOwnership::Unspecified;
      if (current_.kind == TokenKind::Borrow ||
          current_.kind == TokenKind::Consume) {
        if (!is_external && current_.kind == TokenKind::Consume)
          throw CompileError{
              current_.location,
              "consume parameter qualifiers are only supported on external "
              "functions"};
        const bool is_borrow = current_.kind == TokenKind::Borrow;
        ownership = is_borrow ? ast::ParameterOwnership::Borrow
                              : ast::ParameterOwnership::Consume;
        advance();
        if (is_borrow && current_.kind == TokenKind::Var) {
          if (is_external)
            throw CompileError{
                current_.location,
                "mutable borrow parameters are not supported on external "
                "functions"};
          ownership = ast::ParameterOwnership::BorrowMutable;
          advance();
        }
      }
      const Token parameter_name = expect(TokenKind::Identifier);
      static_cast<void>(expect(TokenKind::Colon));
      ast::TypeReference parameter_type = parse_type();
      parameters.push_back(ast::FunctionDeclaration::Parameter{
          std::string{parameter_name.lexeme}, std::move(parameter_type),
          parameter_name.location, ownership, is_scoped});
      if (current_.kind != TokenKind::Comma) {
        break;
      }
      advance();
    } while (true);
  }
  static_cast<void>(expect(TokenKind::RightParen));
  static_cast<void>(expect(TokenKind::Colon));
  ast::ReturnOwnership return_ownership = ast::ReturnOwnership::Unspecified;
  const bool has_owned_return =
      current_.kind == TokenKind::Identifier && current_.lexeme == "owned";
  if (current_.kind == TokenKind::Borrow || has_owned_return) {
    if (!is_external && has_owned_return)
      throw CompileError{
          current_.location,
          "owned return qualifiers are only supported on external functions"};
    return_ownership = current_.kind == TokenKind::Borrow
                           ? ast::ReturnOwnership::Borrow
                           : ast::ReturnOwnership::Owned;
    advance();
    if (return_ownership == ast::ReturnOwnership::Borrow &&
        current_.kind == TokenKind::Var) {
      if (is_external)
        throw CompileError{
            current_.location,
            "mutable borrow returns are not supported on external functions"};
      return_ownership = ast::ReturnOwnership::BorrowMutable;
      advance();
    }
  }
  ast::TypeReference return_type = parse_type();
  if (current_.kind == TokenKind::Identifier && current_.lexeme == "where") {
    advance();
    do {
      const Token parameter = expect(TokenKind::Identifier);
      static_cast<void>(expect(TokenKind::Less));
      static_cast<void>(expect(TokenKind::Colon));
      do {
        type_constraints.push_back(ast::TypeConstraint{
            std::string{parameter.lexeme}, parse_type(), parameter.location});
        if (current_.kind != TokenKind::Ampersand)
          break;
        advance();
      } while (true);
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
  }
  std::vector<ast::Statement> body;
  std::optional<SourceLocation> expression_body_arrow;
  std::optional<SourceLocation> expression_body_start;
  std::size_t expression_body_end = 0;
  if (!is_external) {
    if (current_.kind == TokenKind::Arrow) {
      expression_body_arrow = current_.location;
      advance();
      const SourceLocation expression_location = current_.location;
      expression_body_start = expression_location;
      body.emplace_back(ast::ReturnStatement{
          std::optional<ast::Expression>{parse_expression()},
          expression_location});
      expression_body_end = current_.location.offset;
      while (expression_body_end > expression_location.offset &&
             std::isspace(static_cast<unsigned char>(
                 source_[expression_body_end - 1])))
        --expression_body_end;
    } else {
      body = parse_block();
    }
  }

  ast::FunctionDeclaration declaration{std::string{name.lexeme},
                                       std::move(type_parameters),
                                       std::move(parameters),
                                       std::move(return_type),
                                       std::move(body),
                                       is_external ? declaration_location
                                                   : def.location,
                                       false,
                                       false,
                                       std::move(type_constraints),
                                       is_external,
                                       std::move(external_symbol),
                                       is_variadic,
                                       std::nullopt,
                                       false,
                                       {},
                                       return_ownership,
                                       false,
                                       false,
                                       false,
                                       {},
                                       {},
                                       0};
  declaration.is_constant = is_constant;
  declaration.is_tailrec = is_tailrec;
  declaration.expression_body_arrow = expression_body_arrow;
  declaration.expression_body_start = expression_body_start;
  declaration.expression_body_end = expression_body_end;
  return declaration;
}

ast::Statement Parser::parse_statement() {
  if (current_.kind == TokenKind::Const) {
    const SourceLocation location = current_.location;
    advance();
    return parse_variable_declaration(true, location);
  }
  if (current_.kind == TokenKind::Val || current_.kind == TokenKind::Var ||
      current_.kind == TokenKind::Borrow) {
    return parse_variable_declaration();
  }
  if (current_.kind == TokenKind::Identifier) {
    return starts_assignment() ? ast::Statement{parse_assignment_statement()}
                               : ast::Statement{parse_expression_statement()};
  }
  if (current_.kind == TokenKind::Return) {
    return parse_return_statement();
  }
  if (current_.kind == TokenKind::Delete) {
    return parse_delete_statement();
  }
  if (current_.kind == TokenKind::Defer) {
    return parse_defer_statement();
  }
  if (current_.kind == TokenKind::Break) {
    return parse_break_statement();
  }
  if (current_.kind == TokenKind::Continue) {
    return parse_continue_statement();
  }
  if (current_.kind == TokenKind::If) {
    return parse_if_statement();
  }
  if (current_.kind == TokenKind::While) {
    return parse_while_statement();
  }
  if (current_.kind == TokenKind::For) {
    return parse_for_statement();
  }

  throw CompileError{current_.location,
                     "expected declaration, assignment or 'return', found " +
                         std::string{token_name(current_.kind)}};
}

ast::ValueDeclaration
Parser::parse_variable_declaration(bool is_constant,
                                   SourceLocation constant_location) {
  const bool is_borrowed = current_.kind == TokenKind::Borrow;
  if (is_borrowed)
    advance();
  const bool is_mutable = current_.kind == TokenKind::Var;
  const Token declaration =
      is_constant ? Token{TokenKind::Const, "const", constant_location}
                  : expect(is_mutable ? TokenKind::Var : TokenKind::Val);
  const Token identifier = expect(TokenKind::Identifier);
  std::optional<ast::TypeReference> declared_type;
  if (current_.kind == TokenKind::Colon) {
    advance();
    declared_type.emplace(parse_type());
  }
  std::optional<ast::Expression> initializer;
  if (current_.kind == TokenKind::Equal) {
    advance();
    initializer.emplace(parse_expression());
  } else if (!declared_type.has_value()) {
    throw CompileError{declaration.location,
                       "inferred local '" + std::string{identifier.lexeme} +
                           "' requires an initializer; help: add an explicit "
                           "type annotation"};
  } else if (is_borrowed || !is_mutable) {
    static_cast<void>(expect(TokenKind::Equal));
  }

  ast::ValueDeclaration result{std::string{identifier.lexeme},
                               std::move(declared_type),
                               is_mutable,
                               std::move(initializer),
                               declaration.location,
                               false,
                               false,
                               {}};
  result.is_borrowed = is_borrowed;
  result.is_constant = is_constant;
  return result;
}

ast::Program::StaticAssertion Parser::parse_static_assertion() {
  const Token assertion = expect(TokenKind::StaticAssert);
  static_cast<void>(expect(TokenKind::LeftParen));
  ast::Expression condition = parse_expression();
  std::optional<std::string> message;
  if (current_.kind == TokenKind::Comma) {
    advance();
    message = decode_string_literal(expect(TokenKind::StringLiteral));
  }
  static_cast<void>(expect(TokenKind::RightParen));
  return {std::move(condition), std::move(message), assertion.location, {}, {}};
}

ast::AssignmentStatement Parser::parse_assignment_statement() {
  ast::Expression target = parse_expression();
  const SourceLocation target_location = std::visit(
      [](const auto &node) { return node.location; }, target.value);
  const Token operation = current_;
  advance();
  ast::AssignmentOperator assignment_operation;
  switch (operation.kind) {
  case TokenKind::Equal: assignment_operation = ast::AssignmentOperator::Assign; break;
  case TokenKind::PlusEqual: assignment_operation = ast::AssignmentOperator::Add; break;
  case TokenKind::MinusEqual: assignment_operation = ast::AssignmentOperator::Subtract; break;
  case TokenKind::StarEqual: assignment_operation = ast::AssignmentOperator::Multiply; break;
  case TokenKind::SlashEqual: assignment_operation = ast::AssignmentOperator::Divide; break;
  case TokenKind::PercentEqual: assignment_operation = ast::AssignmentOperator::Remainder; break;
  case TokenKind::AmpersandEqual: assignment_operation = ast::AssignmentOperator::BitwiseAnd; break;
  case TokenKind::PipeEqual: assignment_operation = ast::AssignmentOperator::BitwiseOr; break;
  case TokenKind::CaretEqual: assignment_operation = ast::AssignmentOperator::BitwiseXor; break;
  case TokenKind::ShiftLeftEqual: assignment_operation = ast::AssignmentOperator::ShiftLeft; break;
  case TokenKind::ShiftRightEqual: assignment_operation = ast::AssignmentOperator::ShiftRight; break;
  default:
    throw CompileError{operation.location, "expected assignment operator"};
  }
  std::string object;
  std::string name;
  std::unique_ptr<ast::IndexExpression> index_target;
  if (auto *identifier =
          std::get_if<ast::IdentifierExpression>(&target.value)) {
    name = std::move(identifier->name);
  } else if (auto *member =
                 std::get_if<ast::MemberAccessExpression>(&target.value)) {
    auto *identifier =
        std::get_if<ast::IdentifierExpression>(&member->object->value);
    if (identifier == nullptr)
      throw CompileError{target_location, "invalid assignment target"};
    object = std::move(identifier->name);
    name = std::move(member->member);
  } else if (auto *index = std::get_if<ast::IndexExpression>(&target.value)) {
    index_target = std::make_unique<ast::IndexExpression>(std::move(*index));
  } else {
    throw CompileError{target_location, "invalid assignment target"};
  }
  ast::AssignmentStatement result{std::move(object), std::move(name),
                                  parse_expression(), target_location,
                                  ast::AssignmentOperator::Assign, nullptr};
  result.operation = assignment_operation;
  result.index_target = std::move(index_target);
  return result;
}

ast::DeleteStatement Parser::parse_delete_statement() {
  const Token delete_token = expect(TokenKind::Delete);
  return ast::DeleteStatement{parse_expression(), delete_token.location};
}

ast::DeferStatement Parser::parse_defer_statement() {
  const Token defer_token = expect(TokenKind::Defer);
  if (current_.kind == TokenKind::Delete)
    return ast::DeferStatement{parse_delete_statement(), defer_token.location};
  ast::ExpressionStatement action = parse_expression_statement();
  if (!std::holds_alternative<ast::CallExpression>(action.expression.value) &&
      !std::holds_alternative<ast::MethodCallExpression>(
          action.expression.value))
    throw CompileError{action.location,
                       "defer requires delete, a function call, or a method "
                       "call"};
  return ast::DeferStatement{std::move(action), defer_token.location};
}

ast::BreakStatement Parser::parse_break_statement() {
  return ast::BreakStatement{expect(TokenKind::Break).location};
}

ast::ContinueStatement Parser::parse_continue_statement() {
  return ast::ContinueStatement{expect(TokenKind::Continue).location};
}

ast::ReturnStatement Parser::parse_return_statement() {
  const Token return_token = expect(TokenKind::Return);
  std::optional<ast::Expression> expression;
  if (current_.kind != TokenKind::RightBrace &&
      current_.kind != TokenKind::Semicolon)
    expression.emplace(parse_expression());
  return ast::ReturnStatement{std::move(expression), return_token.location};
}

ast::ExpressionStatement Parser::parse_expression_statement() {
  const SourceLocation location = current_.location;
  return ast::ExpressionStatement{parse_expression(), location};
}

std::shared_ptr<ast::IfStatement> Parser::parse_if_statement() {
  const Token if_token = expect(TokenKind::If);
  ast::Expression condition = parse_expression();
  std::vector<ast::Statement> then_body = parse_block();
  std::vector<ast::Statement> else_body;
  if (current_.kind == TokenKind::Else) {
    advance();
    if (current_.kind == TokenKind::If)
      else_body.emplace_back(parse_if_statement());
    else
      else_body = parse_block();
  }
  return std::make_shared<ast::IfStatement>(
      ast::IfStatement{std::move(condition), std::move(then_body),
                       std::move(else_body), if_token.location});
}

std::shared_ptr<ast::WhileStatement> Parser::parse_while_statement() {
  const Token while_token = expect(TokenKind::While);
  ast::Expression condition = parse_expression();
  return std::make_shared<ast::WhileStatement>(ast::WhileStatement{
      std::move(condition), parse_block(), while_token.location});
}

std::shared_ptr<ast::ForStatement> Parser::parse_for_statement() {
  const Token for_token = expect(TokenKind::For);
  const Token binding = expect(TokenKind::Identifier);
  static_cast<void>(expect(TokenKind::In));
  ast::Expression iterator = parse_expression();
  return std::make_shared<ast::ForStatement>(
      ast::ForStatement{std::string{binding.lexeme}, std::move(iterator),
                        parse_block(), for_token.location});
}

ast::Expression Parser::parse_expression() { return parse_pipeline(); }

ast::Expression Parser::parse_pipeline() {
  ast::Expression expression = parse_logical_or();
  const auto is_qualified_callee = [](const ast::Expression &candidate,
                                      const auto &self) -> bool {
    if (std::holds_alternative<ast::IdentifierExpression>(candidate.value))
      return true;
    if (const auto *member =
            std::get_if<ast::MemberAccessExpression>(&candidate.value))
      return self(*member->object, self);
    return false;
  };
  while (current_.kind == TokenKind::PipeGreater) {
    const Token operation = expect(TokenKind::PipeGreater);
    ast::Expression target = parse_logical_or();
    auto argument =
        std::make_unique<ast::Expression>(std::move(expression));

    if (auto *identifier =
            std::get_if<ast::IdentifierExpression>(&target.value)) {
      std::vector<std::unique_ptr<ast::Expression>> arguments;
      arguments.push_back(std::move(argument));
      expression = ast::CallExpression{identifier->name, {},
                                       std::move(arguments),
                                       identifier->location};
      continue;
    }
    if (auto *call = std::get_if<ast::CallExpression>(&target.value)) {
      call->arguments.insert(call->arguments.begin(), std::move(argument));
      expression = std::move(target);
      continue;
    }
    if (auto *member =
            std::get_if<ast::MemberAccessExpression>(&target.value)) {
      if (!is_qualified_callee(*member->object, is_qualified_callee))
        throw CompileError{
            operation.location,
            "pipeline call target must be a stable qualified name"};
      std::vector<std::unique_ptr<ast::Expression>> arguments;
      arguments.push_back(std::move(argument));
      expression = ast::MethodCallExpression{
          std::move(member->object), member->member, {}, std::move(arguments),
          member->location};
      continue;
    }
    if (auto *call = std::get_if<ast::MethodCallExpression>(&target.value)) {
      if (!is_qualified_callee(*call->object, is_qualified_callee))
        throw CompileError{
            operation.location,
            "pipeline call target must be a stable qualified name"};
      call->arguments.insert(call->arguments.begin(), std::move(argument));
      expression = std::move(target);
      continue;
    }

    throw CompileError{
        operation.location,
        "pipeline right-hand side must be a function or function call"};
  }
  return expression;
}

ast::Expression Parser::parse_logical_or() {
  ast::Expression expression = parse_logical_and();
  while (current_.kind == TokenKind::PipePipe) {
    const Token operation = current_;
    advance();
    expression = ast::BinaryExpression{
        ast::BinaryOperator::LogicalOr,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_logical_and()),
        operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_logical_and() {
  ast::Expression expression = parse_bitwise_or();
  while (current_.kind == TokenKind::AmpAmp) {
    const Token operation = current_;
    advance();
    expression = ast::BinaryExpression{
        ast::BinaryOperator::LogicalAnd,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_bitwise_or()),
        operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_bitwise_or() {
  ast::Expression expression = parse_bitwise_xor();
  while (current_.kind == TokenKind::Pipe) {
    const Token operation = current_;
    advance();
    expression = ast::BinaryExpression{
        ast::BinaryOperator::BitwiseOr,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_bitwise_xor()),
        operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_bitwise_xor() {
  ast::Expression expression = parse_bitwise_and();
  while (current_.kind == TokenKind::Caret) {
    const Token operation = current_;
    advance();
    expression = ast::BinaryExpression{
        ast::BinaryOperator::BitwiseXor,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_bitwise_and()),
        operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_bitwise_and() {
  ast::Expression expression = parse_equality();
  while (current_.kind == TokenKind::Ampersand) {
    const Token operation = current_;
    advance();
    expression = ast::BinaryExpression{
        ast::BinaryOperator::BitwiseAnd,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_equality()),
        operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_equality() {
  ast::Expression expression = parse_comparison();
  while (current_.kind == TokenKind::EqualEqual ||
         current_.kind == TokenKind::BangEqual) {
    const Token operation = current_;
    advance();
    expression = ast::BinaryExpression{
        operation.kind == TokenKind::EqualEqual ? ast::BinaryOperator::Equal
                                                : ast::BinaryOperator::NotEqual,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_comparison()),
        operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_comparison() {
  ast::Expression expression = parse_shift();
  while (current_.kind == TokenKind::Less ||
         current_.kind == TokenKind::LessEqual ||
         current_.kind == TokenKind::Greater ||
         current_.kind == TokenKind::GreaterEqual) {
    const Token operation = current_;
    advance();
    ast::BinaryOperator binary_operation = ast::BinaryOperator::Less;
    if (operation.kind == TokenKind::LessEqual) {
      binary_operation = ast::BinaryOperator::LessEqual;
    } else if (operation.kind == TokenKind::Greater) {
      binary_operation = ast::BinaryOperator::Greater;
    } else if (operation.kind == TokenKind::GreaterEqual) {
      binary_operation = ast::BinaryOperator::GreaterEqual;
    }
    expression = ast::BinaryExpression{
        binary_operation,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_shift()), operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_shift() {
  ast::Expression expression = parse_additive();
  while (current_.kind == TokenKind::ShiftLeft ||
         current_.kind == TokenKind::ShiftRight) {
    const Token operation = current_;
    advance();
    expression = ast::BinaryExpression{
        operation.kind == TokenKind::ShiftLeft
            ? ast::BinaryOperator::ShiftLeft
            : ast::BinaryOperator::ShiftRight,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_additive()),
        operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_additive() {
  ast::Expression expression = parse_multiplicative();
  while (current_.kind == TokenKind::Plus ||
         current_.kind == TokenKind::Minus) {
    const Token operation = current_;
    advance();
    expression = ast::BinaryExpression{
        operation.kind == TokenKind::Plus ? ast::BinaryOperator::Add
                                          : ast::BinaryOperator::Subtract,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_multiplicative()),
        operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_multiplicative() {
  ast::Expression expression = parse_unary();
  while (current_.kind == TokenKind::Star ||
         current_.kind == TokenKind::Slash ||
         current_.kind == TokenKind::Percent) {
    const Token operation = current_;
    advance();
    ast::BinaryOperator binary_operation = ast::BinaryOperator::Multiply;
    if (operation.kind == TokenKind::Slash) {
      binary_operation = ast::BinaryOperator::Divide;
    } else if (operation.kind == TokenKind::Percent) {
      binary_operation = ast::BinaryOperator::Remainder;
    }
    expression = ast::BinaryExpression{
        binary_operation,
        std::make_unique<ast::Expression>(std::move(expression)),
        std::make_unique<ast::Expression>(parse_unary()), operation.location};
  }
  return expression;
}

ast::Expression Parser::parse_unary() {
  if (current_.kind == TokenKind::Move) {
    const Token move_token = expect(TokenKind::Move);
    return ast::MoveExpression{std::make_unique<ast::Expression>(parse_unary()),
                               move_token.location};
  }
  if (current_.kind == TokenKind::Minus || current_.kind == TokenKind::Bang) {
    const Token operation = current_;
    advance();
    if (operation.kind == TokenKind::Minus &&
        current_.kind == TokenKind::IntegerLiteral) {
      const Token literal = expect(TokenKind::IntegerLiteral);
      const auto magnitude = parse_integer_literal(literal.lexeme);
      if (!magnitude) {
        throw CompileError{
            literal.location,
            "integer literal is outside the unsigned 64-bit range"};
      }
      return ast::IntegerLiteralExpression{*magnitude, true,
                                           operation.location};
    }
    return ast::UnaryExpression{
        operation.kind == TokenKind::Minus ? ast::UnaryOperator::Negate
                                           : ast::UnaryOperator::LogicalNot,
        std::make_unique<ast::Expression>(parse_unary()), operation.location};
  }
  return parse_postfix(parse_primary());
}

ast::Expression Parser::parse_primary() {
  if (current_.kind == TokenKind::Identifier) {
    Lexer lookahead = lexer_;
    if (lookahead.next().kind == TokenKind::Arrow) {
      const Token name = expect(TokenKind::Identifier);
      static_cast<void>(expect(TokenKind::Arrow));
      std::vector<ast::LambdaExpression::Parameter> parameters;
      parameters.push_back(ast::LambdaExpression::Parameter{
          std::string{name.lexeme}, std::nullopt, name.location,
          ast::ParameterOwnership::Unspecified});
      if (current_.kind == TokenKind::LeftBrace) {
        const SourceLocation block_location = current_.location;
        return ast::LambdaExpression{
            std::move(parameters),
            std::make_shared<ast::LambdaBlock>(
                ast::LambdaBlock{parse_block(), block_location}),
            name.location};
      }
      return ast::LambdaExpression{
          std::move(parameters),
          std::make_unique<ast::Expression>(parse_expression()),
          name.location};
    }
  }

  if (current_.kind == TokenKind::LeftBracket) {
    const Token opening = expect(TokenKind::LeftBracket);
    std::vector<std::unique_ptr<ast::Expression>> elements;
    if (current_.kind != TokenKind::RightBracket) {
      while (true) {
        elements.push_back(
            std::make_unique<ast::Expression>(parse_expression()));
        if (current_.kind != TokenKind::Comma)
          break;
        advance();
        if (current_.kind == TokenKind::RightBracket)
          break;
      }
    }
    if (current_.kind != TokenKind::RightBracket)
      throw CompileError{current_.location, "expected ']' after array literal"};
    advance();
    return ast::ArrayLiteralExpression{std::move(elements), opening.location};
  }

  if (current_.kind == TokenKind::Match) {
    const Token match_token = expect(TokenKind::Match);
    ast::Expression scrutinee = parse_expression();
    static_cast<void>(expect(TokenKind::LeftBrace));
    std::vector<ast::MatchExpression::Arm> arms;
    while (current_.kind != TokenKind::RightBrace) {
      const Token pattern_token = current_;
      std::vector<ast::MatchPattern> patterns;
      patterns.push_back(parse_match_pattern());
      while (current_.kind == TokenKind::Pipe) {
        advance();
        patterns.push_back(parse_match_pattern());
      }
      std::string case_name;
      std::vector<std::string> bindings;
      ast::Expression *literal = nullptr;
      bool is_wildcard = false;
      const ast::MatchPattern &first_pattern = patterns.front();
      const ast::MatchPattern *compatibility_pattern = &first_pattern;
      if (first_pattern.kind == ast::MatchPattern::Kind::Alias)
        compatibility_pattern = first_pattern.nested.get();
      if (compatibility_pattern->kind == ast::MatchPattern::Kind::Constructor) {
        case_name = compatibility_pattern->name;
        literal = compatibility_pattern->literal.get();
        for (const auto &child : compatibility_pattern->children)
          bindings.push_back(child.kind == ast::MatchPattern::Kind::Name
                                 ? child.name
                                 : std::string{});
      } else if (compatibility_pattern->kind == ast::MatchPattern::Kind::Name) {
        case_name = compatibility_pattern->name;
      } else if (compatibility_pattern->kind ==
                 ast::MatchPattern::Kind::Literal) {
        literal = compatibility_pattern->literal.get();
        if (const auto *call =
                std::get_if<ast::CallExpression>(&literal->value)) {
          case_name = call->callee;
          for (const auto &argument : call->arguments) {
            const auto *identifier =
                std::get_if<ast::IdentifierExpression>(&argument->value);
            if (identifier == nullptr) {
              bindings.clear();
              break;
            }
            bindings.push_back(identifier->name);
          }
        }
      } else {
        is_wildcard = true;
      }
      std::unique_ptr<ast::Expression> guard;
      if (current_.kind == TokenKind::If) {
        advance();
        Lexer guard_lookahead = lexer_;
        if (current_.kind == TokenKind::Identifier &&
            guard_lookahead.next().kind == TokenKind::Arrow) {
          // In `case if predicate => body`, the arrow terminates the match
          // arm; it does not start a bare-parameter lambda.
          const Token identifier = expect(TokenKind::Identifier);
          guard = std::make_unique<ast::Expression>(
              ast::IdentifierExpression{std::string{identifier.lexeme},
                                        identifier.location});
        } else {
          guard = std::make_unique<ast::Expression>(parse_expression());
        }
      }
      static_cast<void>(expect(TokenKind::Arrow));
      arms.push_back(ast::MatchExpression::Arm{
          std::move(patterns), std::move(case_name), std::move(bindings),
          literal, is_wildcard, std::move(guard),
          std::make_unique<ast::Expression>(parse_expression()),
          pattern_token.location});
      if (current_.kind == TokenKind::Comma ||
          current_.kind == TokenKind::Semicolon)
        advance();
    }
    static_cast<void>(expect(TokenKind::RightBrace));
    return ast::MatchExpression{
        std::make_unique<ast::Expression>(std::move(scrutinee)),
        std::move(arms), match_token.location};
  }

  if (current_.kind == TokenKind::If) {
    const Token if_token = expect(TokenKind::If);
    ast::Expression condition = parse_expression();
    static_cast<void>(expect(TokenKind::LeftBrace));
    ast::Expression then_expression = parse_expression();
    static_cast<void>(expect(TokenKind::RightBrace));
    static_cast<void>(expect(TokenKind::Else));
    static_cast<void>(expect(TokenKind::LeftBrace));
    ast::Expression else_expression = parse_expression();
    static_cast<void>(expect(TokenKind::RightBrace));
    return ast::IfExpression{
        std::make_unique<ast::Expression>(std::move(condition)),
        std::make_unique<ast::Expression>(std::move(then_expression)),
        std::make_unique<ast::Expression>(std::move(else_expression)),
        if_token.location};
  }

  if (current_.kind == TokenKind::LeftParen) {
    if (starts_lambda()) {
      const Token left_parenthesis = expect(TokenKind::LeftParen);
      std::vector<ast::LambdaExpression::Parameter> parameters;
      if (current_.kind != TokenKind::RightParen) {
        do {
          ast::ParameterOwnership ownership =
              ast::ParameterOwnership::Unspecified;
          if (current_.kind == TokenKind::Borrow) {
            advance();
            ownership = ast::ParameterOwnership::Borrow;
            if (current_.kind == TokenKind::Var) {
              advance();
              ownership = ast::ParameterOwnership::BorrowMutable;
            }
          }
          const Token name = expect(TokenKind::Identifier);
          std::optional<ast::TypeReference> type;
          if (current_.kind == TokenKind::Colon) {
            advance();
            type = parse_type();
          }
          parameters.push_back(ast::LambdaExpression::Parameter{
              std::string{name.lexeme}, std::move(type), name.location,
              ownership});
          if (current_.kind != TokenKind::Comma)
            break;
          advance();
        } while (true);
      }
      static_cast<void>(expect(TokenKind::RightParen));
      static_cast<void>(expect(TokenKind::Arrow));
      if (current_.kind == TokenKind::LeftBrace) {
        const SourceLocation block_location = current_.location;
        return ast::LambdaExpression{
            std::move(parameters),
            std::make_shared<ast::LambdaBlock>(
                ast::LambdaBlock{parse_block(), block_location}),
            left_parenthesis.location};
      }
      return ast::LambdaExpression{
          std::move(parameters),
          std::make_unique<ast::Expression>(parse_expression()),
          left_parenthesis.location};
    }
    advance();
    ast::Expression expression = parse_expression();
    static_cast<void>(expect(TokenKind::RightParen));
    return expression;
  }

  if (current_.kind == TokenKind::New) {
    const Token new_token = expect(TokenKind::New);
    const std::string class_name = parse_qualified_name();
    std::vector<ast::TypeReference> type_arguments;
    if (current_.kind == TokenKind::LeftBracket) {
      advance();
      do {
        type_arguments.push_back(parse_type());
        if (current_.kind != TokenKind::Comma)
          break;
        advance();
      } while (true);
      static_cast<void>(expect(TokenKind::RightBracket));
    }
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
    return ast::NewExpression{class_name, std::move(type_arguments),
                              std::move(arguments), new_token.location};
  }

  if (current_.kind == TokenKind::Identifier) {
    const Token identifier = expect(TokenKind::Identifier);
    std::vector<ast::TypeReference> type_arguments;

    if (current_.kind == TokenKind::LeftBracket && starts_generic_call()) {
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
    return ast::IdentifierExpression{std::string{identifier.lexeme},
                                     identifier.location};
  }

  if (current_.kind == TokenKind::IntegerLiteral) {
    const Token literal = expect(TokenKind::IntegerLiteral);
    const auto value = parse_integer_literal(literal.lexeme);
    if (!value) {
      throw CompileError{
          literal.location,
          "integer literal is outside the unsigned 64-bit range"};
    }

    return ast::IntegerLiteralExpression{*value, false, literal.location};
  }

  if (current_.kind == TokenKind::DoubleLiteral ||
      current_.kind == TokenKind::FloatLiteral) {
    const bool is_float = current_.kind == TokenKind::FloatLiteral;
    const Token literal = current_;
    advance();
    const std::string text{
        is_float ? literal.lexeme.substr(0, literal.lexeme.size() - 1)
                 : literal.lexeme};
    char *end = nullptr;
    errno = 0;
    const double value =
        is_float ? static_cast<double>(std::strtof(text.c_str(), &end))
                 : std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end != text.c_str() + text.size() ||
        !std::isfinite(value)) {
      throw CompileError{literal.location, is_float ? "invalid float literal"
                                                    : "invalid double literal"};
    }
    return ast::DoubleLiteralExpression{value, is_float, literal.location};
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

  throw CompileError{
      DiagnosticCode::ParserExpectedExpression, current_.location,
      "expected expression, found " + std::string{token_name(current_.kind)}};
}

ast::MatchPattern Parser::parse_match_pattern() {
  const Token token = current_;
  const bool literal_start = current_.kind == TokenKind::IntegerLiteral ||
                             current_.kind == TokenKind::FloatLiteral ||
                             current_.kind == TokenKind::DoubleLiteral ||
                             current_.kind == TokenKind::StringLiteral ||
                             current_.kind == TokenKind::CharacterLiteral ||
                             current_.kind == TokenKind::Minus ||
                             current_.kind == TokenKind::True ||
                             current_.kind == TokenKind::False;
  ast::MatchPattern pattern;
  pattern.location = token.location;
  if (literal_start) {
    pattern.kind = ast::MatchPattern::Kind::Literal;
    pattern.literal = std::make_unique<ast::Expression>(parse_additive());
  } else {
    const Token name = expect(TokenKind::Identifier);
    pattern.name = std::string{name.lexeme};
    if (pattern.name == "_") {
      pattern.kind = ast::MatchPattern::Kind::Wildcard;
    } else if (current_.kind == TokenKind::LeftParen) {
      static const std::unordered_set<std::string> literal_conversions{
          "byte", "ubyte", "short",  "ushort", "int",
          "uint", "long",  "ulong",  "isize",  "usize",
          "char", "bool",  "string", "float",  "double"};
      advance();
      if (literal_conversions.contains(pattern.name)) {
        std::vector<std::unique_ptr<ast::Expression>> arguments;
        if (current_.kind != TokenKind::RightParen) {
          arguments.push_back(
              std::make_unique<ast::Expression>(parse_expression()));
          while (current_.kind == TokenKind::Comma) {
            advance();
            arguments.push_back(
                std::make_unique<ast::Expression>(parse_expression()));
          }
        }
        static_cast<void>(expect(TokenKind::RightParen));
        // Conversion syntax is resolved from the scrutinee type: it can be a
        // scalar literal (`int(1)`) or a homonymous enum constructor
        // (`enum E { int(int) }`).
        pattern.kind = ast::MatchPattern::Kind::Constructor;
        pattern.literal = std::make_unique<ast::Expression>(ast::CallExpression{
            pattern.name, {}, std::move(arguments), token.location});
        const auto &call =
            std::get<ast::CallExpression>(pattern.literal->value);
        for (const auto &argument : call.arguments) {
          const auto *identifier =
              std::get_if<ast::IdentifierExpression>(&argument->value);
          if (identifier == nullptr) {
            pattern.children.clear();
            break;
          }
          ast::MatchPattern binding;
          binding.kind = ast::MatchPattern::Kind::Name;
          binding.name = identifier->name;
          binding.location = identifier->location;
          pattern.children.push_back(std::move(binding));
        }
      } else {
        pattern.kind = ast::MatchPattern::Kind::Constructor;
        if (current_.kind != TokenKind::RightParen) {
          do {
            pattern.children.push_back(parse_match_pattern());
            if (current_.kind != TokenKind::Comma)
              break;
            advance();
          } while (true);
        }
        static_cast<void>(expect(TokenKind::RightParen));
      }
    } else {
      pattern.kind = ast::MatchPattern::Kind::Name;
    }
  }
  if (current_.kind == TokenKind::As) {
    advance();
    const Token alias = expect(TokenKind::Identifier);
    ast::MatchPattern aliased;
    aliased.kind = ast::MatchPattern::Kind::Alias;
    aliased.name = std::string{alias.lexeme};
    aliased.location = alias.location;
    aliased.nested = std::make_unique<ast::MatchPattern>(std::move(pattern));
    return aliased;
  }
  return pattern;
}

ast::TypeReference Parser::parse_type() {
  if (current_.kind == TokenKind::LeftParen) {
    const Token left_parenthesis = expect(TokenKind::LeftParen);
    std::vector<ast::TypeReference> arguments;
    std::vector<ast::ParameterOwnership> ownerships;
    if (current_.kind != TokenKind::RightParen) {
      do {
        ast::ParameterOwnership ownership =
            ast::ParameterOwnership::Unspecified;
        if (current_.kind == TokenKind::Borrow) {
          advance();
          ownership = ast::ParameterOwnership::Borrow;
          if (current_.kind == TokenKind::Var) {
            advance();
            ownership = ast::ParameterOwnership::BorrowMutable;
          }
        }
        arguments.push_back(parse_type());
        ownerships.push_back(ownership);
        if (current_.kind != TokenKind::Comma)
          break;
        advance();
      } while (true);
    }
    static_cast<void>(expect(TokenKind::RightParen));
    static_cast<void>(expect(TokenKind::Arrow));
    ast::ReturnOwnership return_ownership =
        ast::ReturnOwnership::Unspecified;
    if (current_.kind == TokenKind::Borrow) {
      advance();
      return_ownership = current_.kind == TokenKind::Var
                             ? ast::ReturnOwnership::BorrowMutable
                             : ast::ReturnOwnership::Borrow;
      if (current_.kind == TokenKind::Var)
        advance();
    }
    arguments.push_back(parse_type());
    ast::TypeReference result{"Function", left_parenthesis.location,
                              std::move(arguments)};
    result.function_parameter_ownership = std::move(ownerships);
    result.function_return_ownership = return_ownership;
    return result;
  }
  const Token type_name = expect(TokenKind::Identifier);
  std::string qualified_type_name{type_name.lexeme};
  while (current_.kind == TokenKind::Dot) {
    advance();
    const Token component = expect(TokenKind::Identifier);
    qualified_type_name += "." + std::string{component.lexeme};
  }
  std::vector<ast::TypeReference> type_arguments;
  if (current_.kind == TokenKind::LeftBracket) {
    advance();
    do {
      type_arguments.push_back(parse_type());
      if (current_.kind != TokenKind::Comma)
        break;
      advance();
    } while (true);
    static_cast<void>(expect(TokenKind::RightBracket));
  }
  return ast::TypeReference{std::move(qualified_type_name), type_name.location,
                            std::move(type_arguments)};
}

ast::Expression Parser::parse_postfix(ast::Expression expression) {
  while (current_.kind == TokenKind::Dot ||
         current_.kind == TokenKind::LeftBracket ||
         current_.kind == TokenKind::Question) {
    if (current_.kind == TokenKind::LeftBracket) {
      const Token bracket = expect(TokenKind::LeftBracket);
      ast::Expression index = parse_expression();
      static_cast<void>(expect(TokenKind::RightBracket));
      expression = ast::IndexExpression{
          std::make_unique<ast::Expression>(std::move(expression)),
          std::make_unique<ast::Expression>(std::move(index)),
          bracket.location};
      continue;
    }
    if (current_.kind == TokenKind::Question) {
      const Token question = expect(TokenKind::Question);
      expression = ast::TryExpression{
          std::make_unique<ast::Expression>(std::move(expression)),
          question.location};
      continue;
    }
    advance();
    const Token member = expect(TokenKind::Identifier);
    std::vector<ast::TypeReference> type_arguments;
    if (current_.kind == TokenKind::LeftBracket && starts_generic_call()) {
      advance();
      do {
        type_arguments.push_back(parse_type());
        if (current_.kind != TokenKind::Comma)
          break;
        advance();
      } while (true);
      static_cast<void>(expect(TokenKind::RightBracket));
    }

    auto object = std::make_unique<ast::Expression>(std::move(expression));
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
      expression = ast::MethodCallExpression{
          std::move(object), std::string{member.lexeme},
          std::move(type_arguments), std::move(arguments), member.location};
      continue;
    }
    if (!type_arguments.empty())
      throw CompileError{current_.location,
                         "expected '(' after method type arguments"};
    expression = ast::MemberAccessExpression{
        std::move(object), std::string{member.lexeme}, member.location};
  }
  return expression;
}

bool Parser::starts_lambda() const {
  if (current_.kind != TokenKind::LeftParen)
    return false;
  Lexer lookahead = lexer_;
  const Token first = lookahead.next();
  if (first.kind == TokenKind::RightParen)
    return lookahead.next().kind == TokenKind::Arrow;
  Token token = first;
  while (true) {
    if (token.kind == TokenKind::Borrow) {
      token = lookahead.next();
      if (token.kind == TokenKind::Var)
        token = lookahead.next();
    }
    if (token.kind != TokenKind::Identifier)
      return false;
    token = lookahead.next();
    if (token.kind == TokenKind::Colon) {
      int brackets = 0;
      do {
        token = lookahead.next();
        if (token.kind == TokenKind::LeftBracket ||
            token.kind == TokenKind::LeftParen)
          ++brackets;
        else if (token.kind == TokenKind::RightBracket ||
                 (token.kind == TokenKind::RightParen && brackets > 0))
          --brackets;
      } while (brackets > 0 ||
               (token.kind != TokenKind::Comma &&
                token.kind != TokenKind::RightParen &&
                token.kind != TokenKind::End));
    }
    if (token.kind == TokenKind::RightParen)
      return lookahead.next().kind == TokenKind::Arrow;
    if (token.kind != TokenKind::Comma)
      return false;
    token = lookahead.next();
  }
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

bool Parser::starts_assignment() const {
  const auto is_assignment_operator = [](TokenKind kind) {
    return kind == TokenKind::Equal || kind == TokenKind::PlusEqual ||
           kind == TokenKind::MinusEqual || kind == TokenKind::StarEqual ||
           kind == TokenKind::SlashEqual || kind == TokenKind::PercentEqual ||
           kind == TokenKind::AmpersandEqual || kind == TokenKind::PipeEqual ||
           kind == TokenKind::CaretEqual || kind == TokenKind::ShiftLeftEqual ||
           kind == TokenKind::ShiftRightEqual;
  };
  Lexer lookahead = lexer_;
  Token next = lookahead.next();
  int brackets = 0;
  int parentheses = 0;
  while (next.kind != TokenKind::End) {
    bool completed_group = false;
    if (next.kind == TokenKind::LeftBracket)
      ++brackets;
    else if (next.kind == TokenKind::RightBracket) {
      --brackets;
      completed_group = brackets == 0 && parentheses == 0;
    }
    else if (next.kind == TokenKind::LeftParen)
      ++parentheses;
    else if (next.kind == TokenKind::RightParen) {
      --parentheses;
      completed_group = brackets == 0 && parentheses == 0;
    }
    else if (brackets == 0 && parentheses == 0 &&
             is_assignment_operator(next.kind))
      return true;
    else if (brackets == 0 && parentheses == 0 &&
             next.kind != TokenKind::Dot &&
             next.kind != TokenKind::Question &&
             next.kind != TokenKind::Identifier)
      return false;
    next = lookahead.next();
    if (completed_group && !is_assignment_operator(next.kind) &&
        next.kind != TokenKind::Dot &&
        next.kind != TokenKind::LeftBracket &&
        next.kind != TokenKind::Question)
      return false;
    if (brackets == 0 && parentheses == 0 &&
        (next.kind == TokenKind::Val || next.kind == TokenKind::Var ||
         next.kind == TokenKind::Return || next.kind == TokenKind::Delete ||
         next.kind == TokenKind::Defer))
      return false;
  }
  return false;
}

bool Parser::starts_generic_call() const {
  Lexer lookahead = lexer_;
  Token token = lookahead.next();
  TokenKind previous = TokenKind::LeftBracket;
  int depth = 1;
  while (token.kind != TokenKind::End) {
    if (depth == 1 && token.kind == TokenKind::LeftParen &&
        previous == TokenKind::Identifier)
      return false;
    if (token.kind == TokenKind::LeftBracket)
      ++depth;
    else if (token.kind == TokenKind::RightBracket && --depth == 0)
      return lookahead.next().kind == TokenKind::LeftParen;
    previous = token.kind;
    token = lookahead.next();
  }
  return false;
}

void Parser::advance() { current_ = lexer_.next(); }

} // namespace janus::frontend
