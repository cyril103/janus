#include "janus/frontend/lexer.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <cctype>
#include <string>

namespace janus::frontend {

namespace {

bool is_digit_for_base(char character, unsigned base) {
  if (character >= '0' && character <= '9')
    return static_cast<unsigned>(character - '0') < base;
  if (base == 16 && character >= 'a' && character <= 'f')
    return true;
  return base == 16 && character >= 'A' && character <= 'F';
}

} // namespace

Lexer::Lexer(std::string_view source) noexcept : source_{source} {}

Token Lexer::next() {
  skip_whitespace();

  const SourceLocation start = location();
  if (at_end()) {
    return Token{TokenKind::End, {}, start};
  }

  const std::size_t start_position = position_;
  const char character = current();

  if (character == '/' && position_ + 2 < source_.size() &&
      source_[position_ + 1] == '/' && source_[position_ + 2] == '/') {
    advance();
    advance();
    advance();
    const std::size_t content_start = position_;
    while (!at_end() && current() != '\n')
      advance();
    return Token{TokenKind::DocumentationComment,
                 source_.substr(content_start, position_ - content_start),
                 start};
  }

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
    if (lexeme == "module") {
      kind = TokenKind::Module;
    } else if (lexeme == "import") {
      kind = TokenKind::Import;
    } else if (lexeme == "as") {
      kind = TokenKind::As;
    } else if (lexeme == "extern") {
      kind = TokenKind::Extern;
    } else if (lexeme == "tailrec") {
      kind = TokenKind::Tailrec;
    } else if (lexeme == "def") {
      kind = TokenKind::Def;
    } else if (lexeme == "type") {
      kind = TokenKind::Type;
    } else if (lexeme == "trait") {
      kind = TokenKind::Trait;
    } else if (lexeme == "extends") {
      kind = TokenKind::Extends;
    } else if (lexeme == "enum") {
      kind = TokenKind::Enum;
    } else if (lexeme == "class") {
      kind = TokenKind::Class;
    } else if (lexeme == "struct") {
      kind = TokenKind::Struct;
    } else if (lexeme == "derives") {
      kind = TokenKind::Derives;
    } else if (lexeme == "new") {
      kind = TokenKind::New;
    } else if (lexeme == "move") {
      kind = TokenKind::Move;
    } else if (lexeme == "consume") {
      kind = TokenKind::Consume;
    } else if (lexeme == "borrow") {
      kind = TokenKind::Borrow;
    } else if (lexeme == "defer") {
      kind = TokenKind::Defer;
    } else if (lexeme == "delete") {
      kind = TokenKind::Delete;
    } else if (lexeme == "destructor") {
      kind = TokenKind::Destructor;
    } else if (lexeme == "private") {
      kind = TokenKind::Private;
    } else if (lexeme == "internal") {
      kind = TokenKind::Internal;
    } else if (lexeme == "if") {
      kind = TokenKind::If;
    } else if (lexeme == "else") {
      kind = TokenKind::Else;
    } else if (lexeme == "match") {
      kind = TokenKind::Match;
    } else if (lexeme == "for") {
      kind = TokenKind::For;
    } else if (lexeme == "in") {
      kind = TokenKind::In;
    } else if (lexeme == "while") {
      kind = TokenKind::While;
    } else if (lexeme == "break") {
      kind = TokenKind::Break;
    } else if (lexeme == "continue") {
      kind = TokenKind::Continue;
    } else if (lexeme == "return") {
      kind = TokenKind::Return;
    } else if (lexeme == "const") {
      kind = TokenKind::Const;
    } else if (lexeme == "pure") {
      kind = TokenKind::Pure;
    } else if (lexeme == "staticAssert") {
      kind = TokenKind::StaticAssert;
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
    if (character == '0' && position_ + 1 < source_.size() &&
        (source_[position_ + 1] == 'x' || source_[position_ + 1] == 'X' ||
         source_[position_ + 1] == 'b' || source_[position_ + 1] == 'B')) {
      const unsigned base = source_[position_ + 1] == 'x' ||
                                    source_[position_ + 1] == 'X'
                                ? 16
                                : 2;
      advance();
      advance();
      const std::size_t digits_start = position_;
      while (!at_end() &&
             (std::isalnum(static_cast<unsigned char>(current())) != 0 ||
              current() == '_'))
        advance();
      const std::string_view lexeme =
          source_.substr(start_position, position_ - start_position);
      if (position_ == digits_start)
        throw CompileError{start,
                           "prefixed integer literal requires at least one digit"};
      for (std::size_t index = digits_start; index < position_; ++index) {
        const char digit = source_[index];
        if (digit == '_') {
          if (index == digits_start || index + 1 == position_ ||
              !is_digit_for_base(source_[index - 1], base) ||
              !is_digit_for_base(source_[index + 1], base))
            throw CompileError{start,
                               "integer separator must be between two valid digits"};
        } else if (!is_digit_for_base(digit, base)) {
          throw CompileError{start, "invalid digit in base-" +
                                        std::to_string(base) +
                                        " integer literal"};
        }
      }
      return Token{TokenKind::IntegerLiteral, lexeme, start};
    }

    do {
      advance();
    } while (!at_end() &&
             (std::isdigit(static_cast<unsigned char>(current())) != 0 ||
              current() == '_'));

    if (position_ > start_position && source_[position_ - 1] == '_') {
      while (!at_end() &&
             (std::isalnum(static_cast<unsigned char>(current())) != 0 ||
              current() == '_'))
        advance();
      throw CompileError{start,
                         "integer separator must be between two valid digits"};
    }
    for (std::size_t index = start_position + 1; index + 1 < position_;
         ++index) {
      if (source_[index] == '_' &&
          (source_[index - 1] == '_' || source_[index + 1] == '_'))
        throw CompileError{start,
                           "integer separator must be between two valid digits"};
    }

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

    if (!at_end() && (current() == 'e' || current() == 'E')) {
      const std::size_t exponent_start = position_;
      advance();
      if (!at_end() && (current() == '+' || current() == '-'))
        advance();
      const std::size_t digits_start = position_;
      while (!at_end() &&
             std::isdigit(static_cast<unsigned char>(current())) != 0)
        advance();
      if (position_ != digits_start)
        kind = TokenKind::DoubleLiteral;
      else
        position_ = exponent_start;
    }

    if (kind == TokenKind::DoubleLiteral && !at_end() && current() == 'f') {
      kind = TokenKind::FloatLiteral;
      advance();
      while (!at_end() &&
             (std::isalnum(static_cast<unsigned char>(current())) != 0 ||
              current() == '_'))
        advance();
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

  if (character == '.' && position_ + 2 < source_.size() &&
      source_[position_ + 1] == '.' && source_[position_ + 2] == '.') {
    advance();
    advance();
    advance();
    return Token{TokenKind::Ellipsis, source_.substr(start_position, 3), start};
  }

  if (position_ + 2 < source_.size() && source_[position_ + 2] == '=' &&
      ((character == '<' && source_[position_ + 1] == '<') ||
       (character == '>' && source_[position_ + 1] == '>'))) {
    advance();
    advance();
    advance();
    return Token{character == '<' ? TokenKind::ShiftLeftEqual
                                  : TokenKind::ShiftRightEqual,
                 source_.substr(start_position, 3), start};
  }

  if (position_ + 1 < source_.size()) {
    const char next_character = source_[position_ + 1];
    TokenKind kind;
    bool is_two_character_token = true;
    if (character == '=' && next_character == '=') {
      kind = TokenKind::EqualEqual;
    } else if (character == '=' && next_character == '>') {
      kind = TokenKind::Arrow;
    } else if (character == '!' && next_character == '=') {
      kind = TokenKind::BangEqual;
    } else if (character == '<' && next_character == '=') {
      kind = TokenKind::LessEqual;
    } else if (character == '>' && next_character == '=') {
      kind = TokenKind::GreaterEqual;
    } else if (character == '&' && next_character == '&') {
      kind = TokenKind::AmpAmp;
    } else if (character == '|' && next_character == '|') {
      kind = TokenKind::PipePipe;
    } else if (character == '|' && next_character == '>') {
      kind = TokenKind::PipeGreater;
    } else if (character == '<' && next_character == '<') {
      kind = TokenKind::ShiftLeft;
    } else if (character == '>' && next_character == '>') {
      kind = TokenKind::ShiftRight;
    } else if (next_character == '=') {
      switch (character) {
      case '+': kind = TokenKind::PlusEqual; break;
      case '-': kind = TokenKind::MinusEqual; break;
      case '*': kind = TokenKind::StarEqual; break;
      case '/': kind = TokenKind::SlashEqual; break;
      case '%': kind = TokenKind::PercentEqual; break;
      case '&': kind = TokenKind::AmpersandEqual; break;
      case '|': kind = TokenKind::PipeEqual; break;
      case '^': kind = TokenKind::CaretEqual; break;
      default: is_two_character_token = false; break;
      }
    } else {
      is_two_character_token = false;
    }

    if (is_two_character_token) {
      advance();
      advance();
      return Token{kind, source_.substr(start_position, 2), start};
    }
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
  case '!':
    return Token{TokenKind::Bang, source_.substr(start_position, 1), start};
  case '+':
    return Token{TokenKind::Plus, source_.substr(start_position, 1), start};
  case '-':
    return Token{TokenKind::Minus, source_.substr(start_position, 1), start};
  case '*':
    return Token{TokenKind::Star, source_.substr(start_position, 1), start};
  case '/':
    return Token{TokenKind::Slash, source_.substr(start_position, 1), start};
  case '%':
    return Token{TokenKind::Percent, source_.substr(start_position, 1), start};
  case '&':
    return Token{TokenKind::Ampersand, source_.substr(start_position, 1),
                 start};
  case '|':
    return Token{TokenKind::Pipe, source_.substr(start_position, 1), start};
  case '^':
    return Token{TokenKind::Caret, source_.substr(start_position, 1), start};
  case '<':
    return Token{TokenKind::Less, source_.substr(start_position, 1), start};
  case '>':
    return Token{TokenKind::Greater, source_.substr(start_position, 1), start};
  case ';':
    return Token{TokenKind::Semicolon, source_.substr(start_position, 1),
                 start};
  case '.':
    return Token{TokenKind::Dot, source_.substr(start_position, 1), start};
  case '?':
    return Token{TokenKind::Question, source_.substr(start_position, 1), start};
  default:
    SourceLocation end = start;
    ++end.offset;
    ++end.column;
    throw CompileError{
        Diagnostic{DiagnosticSeverity::Error,
                   DiagnosticCode::LexerUnexpectedCharacter,
                   "unexpected character '" + std::string(1, character) + "'",
                   start,
                   {},
                   {},
                   {DiagnosticSuggestion{"remove the unexpected character",
                                         SourceRange{start, end}, ""}}}};
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
        source_[position_ + 1] == '/' &&
        (position_ + 2 >= source_.size() || source_[position_ + 2] != '/')) {
      while (!at_end() && current() != '\n') {
        advance();
      }
      continue;
    }

    break;
  }
}

} // namespace janus::frontend
