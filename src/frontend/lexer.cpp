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
    const TokenKind kind =
        lexeme == "val" ? TokenKind::Val : TokenKind::Identifier;
    return Token{kind, lexeme, start};
  }

  if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
    do {
      advance();
    } while (!at_end() &&
             std::isdigit(static_cast<unsigned char>(current())) != 0);

    return Token{TokenKind::IntegerLiteral,
                 source_.substr(start_position, position_ - start_position),
                 start};
  }

  advance();
  switch (character) {
  case ':':
    return Token{TokenKind::Colon, source_.substr(start_position, 1), start};
  case '=':
    return Token{TokenKind::Equal, source_.substr(start_position, 1), start};
  case ';':
    return Token{TokenKind::Semicolon, source_.substr(start_position, 1),
                 start};
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
  while (!at_end() &&
         std::isspace(static_cast<unsigned char>(current())) != 0) {
    advance();
  }
}

} // namespace janus::frontend
