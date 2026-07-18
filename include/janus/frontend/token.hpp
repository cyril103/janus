#pragma once

#include "janus/diagnostics/compile_error.hpp"

#include <string_view>

namespace janus::frontend {

enum class TokenKind {
  Def,
  Class,
  New,
  Delete,
  Destructor,
  Return,
  Val,
  Var,
  True,
  False,
  Identifier,
  IntegerLiteral,
  DoubleLiteral,
  CharacterLiteral,
  StringLiteral,
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  LeftBracket,
  RightBracket,
  Colon,
  Comma,
  Equal,
  Semicolon,
  Dot,
  End,
};

struct Token {
  TokenKind kind;
  std::string_view lexeme;
  SourceLocation location;
};

[[nodiscard]] constexpr std::string_view token_name(TokenKind kind) noexcept {
  switch (kind) {
  case TokenKind::Def:
    return "'def'";
  case TokenKind::Class:
    return "'class'";
  case TokenKind::New:
    return "'new'";
  case TokenKind::Delete:
    return "'delete'";
  case TokenKind::Destructor:
    return "'destructor'";
  case TokenKind::Return:
    return "'return'";
  case TokenKind::Val:
    return "'val'";
  case TokenKind::Var:
    return "'var'";
  case TokenKind::True:
    return "'true'";
  case TokenKind::False:
    return "'false'";
  case TokenKind::Identifier:
    return "identifier";
  case TokenKind::IntegerLiteral:
    return "integer literal";
  case TokenKind::DoubleLiteral:
    return "double literal";
  case TokenKind::CharacterLiteral:
    return "character literal";
  case TokenKind::StringLiteral:
    return "string literal";
  case TokenKind::LeftParen:
    return "'('";
  case TokenKind::RightParen:
    return "')'";
  case TokenKind::LeftBrace:
    return "'{'";
  case TokenKind::RightBrace:
    return "'}'";
  case TokenKind::LeftBracket:
    return "'['";
  case TokenKind::RightBracket:
    return "']'";
  case TokenKind::Colon:
    return "':'";
  case TokenKind::Comma:
    return "','";
  case TokenKind::Equal:
    return "'='";
  case TokenKind::Semicolon:
    return "';'";
  case TokenKind::Dot:
    return "'.'";
  case TokenKind::End:
    return "end of file";
  }
  return "token";
}

} // namespace janus::frontend
