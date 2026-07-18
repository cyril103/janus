#include "janus/frontend/lexer.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <cctype>
#include <string>

namespace janus::frontend {

Lexer::Lexer(std::string_view source) noexcept : source_{source} {}

Token Lexer::next() {
  skip_whitespace();

  const SourceLocation start = location();
  if (at_end()) {
    return Token{TokenKind::End, {}, start};
  }

  const std::size_t start_position = position_;
  const char character = current();

  if (std::isalpha(static_cast<unsigned char>(character)) != 0 ||
      character == '_') {
    do {
      advance();
    } while (!at_end() &&
             (std::isalnum(static_cast<unsigned char>(current())) != 0 ||
              current() == '_'));

    const std::string_view lexeme =
        source_.substr(start_position, position_ - start_position);
    TokenKind kind = TokenKind::Identifier;
    if (lexeme == "def") {
      kind = TokenKind::Def;
    } else if (lexeme == "class") {
      kind = TokenKind::Class;
    } else if (lexeme == "new") {
      kind = TokenKind::New;
    } else if (lexeme == "delete") {
      kind = TokenKind::Delete;
    } else if (lexeme == "destructor") {
      kind = TokenKind::Destructor;
    } else if (lexeme == "return") {
      kind = TokenKind::Return;
    } else if (lexeme == "val") {
      kind = TokenKind::Val;
    } else if (lexeme == "var") {
      kind = TokenKind::Var;
    } else if (lexeme == "true") {
      kind = TokenKind::True;
    } else if (lexeme == "false") {
      kind = TokenKind::False;
    }
    return Token{kind, lexeme, start};
  }

  if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
    do {
      advance();
    } while (!at_end() &&
             std::isdigit(static_cast<unsigned char>(current())) != 0);

    TokenKind kind = TokenKind::IntegerLiteral;
    if (!at_end() && current() == '.' && position_ + 1 < source_.size() &&
        std::isdigit(static_cast<unsigned char>(source_[position_ + 1])) != 0) {
      kind = TokenKind::DoubleLiteral;
      advance();
      while (!at_end() &&
             std::isdigit(static_cast<unsigned char>(current())) != 0) {
        advance();
      }
    }

    return Token{kind,
                 source_.substr(start_position, position_ - start_position),
                 start};
  }

  if (character == '"') {
    advance();
    bool escaped = false;
    while (!at_end()) {
      if (!escaped && current() == '"') {
        advance();
        return Token{TokenKind::StringLiteral,
                     source_.substr(start_position, position_ - start_position),
                     start};
      }
      if (current() == '\n') {
        break;
      }
      if (!escaped && current() == '\\') {
        escaped = true;
      } else {
        escaped = false;
      }
      advance();
    }
    throw CompileError{start, "unterminated string literal"};
  }

  if (character == '\'') {
    advance();
    bool escaped = false;
    while (!at_end()) {
      if (!escaped && current() == '\'') {
        advance();
        return Token{TokenKind::CharacterLiteral,
                     source_.substr(start_position, position_ - start_position),
                     start};
      }
      if (current() == '\n') {
        break;
      }
      if (!escaped && current() == '\\') {
        escaped = true;
      } else {
        escaped = false;
      }
      advance();
    }
    throw CompileError{start, "unterminated character literal"};
  }

  advance();
  switch (character) {
  case '(':
    return Token{TokenKind::LeftParen, source_.substr(start_position, 1),
                 start};
  case ')':
    return Token{TokenKind::RightParen, source_.substr(start_position, 1),
                 start};
  case '{':
    return Token{TokenKind::LeftBrace, source_.substr(start_position, 1),
                 start};
  case '}':
    return Token{TokenKind::RightBrace, source_.substr(start_position, 1),
                 start};
  case '[':
    return Token{TokenKind::LeftBracket, source_.substr(start_position, 1),
                 start};
  case ']':
    return Token{TokenKind::RightBracket, source_.substr(start_position, 1),
                 start};
  case ':':
    return Token{TokenKind::Colon, source_.substr(start_position, 1), start};
  case ',':
    return Token{TokenKind::Comma, source_.substr(start_position, 1), start};
  case '=':
    return Token{TokenKind::Equal, source_.substr(start_position, 1), start};
  case ';':
    return Token{TokenKind::Semicolon, source_.substr(start_position, 1),
                 start};
  case '.':
    return Token{TokenKind::Dot, source_.substr(start_position, 1), start};
  default:
    throw CompileError{start, "unexpected character '" +
                                  std::string(1, character) + "'"};
  }
}

bool Lexer::at_end() const noexcept { return position_ >= source_.size(); }

char Lexer::current() const noexcept { return source_[position_]; }

SourceLocation Lexer::location() const noexcept {
  return SourceLocation{position_, line_, column_};
}

void Lexer::advance() noexcept {
  if (at_end()) {
    return;
  }

  if (source_[position_] == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  ++position_;
}

void Lexer::skip_whitespace() noexcept {
  while (!at_end()) {
    if (std::isspace(static_cast<unsigned char>(current())) != 0) {
      advance();
      continue;
    }

    if (current() == '/' && position_ + 1 < source_.size() &&
        source_[position_ + 1] == '/') {
      while (!at_end() && current() != '\n') {
        advance();
      }
      continue;
    }

    break;
  }
}

} // namespace janus::frontend
