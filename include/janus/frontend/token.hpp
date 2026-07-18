#pragma once

#include "janus/diagnostics/compile_error.hpp"

#include <string_view>

namespace janus::frontend {

enum class TokenKind {
  Module,
  Import,
  Def,
  Enum,
  Class,
  New,
  Delete,
  Destructor,
  Private,
  If,
  Else,
  Match,
  While,
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
  Arrow,
  EqualEqual,
  Bang,
  BangEqual,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  AmpAmp,
  PipePipe,
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
  case TokenKind::Module:
    return "'module'";
  case TokenKind::Import:
    return "'import'";
  case TokenKind::Def:
    return "'def'";
  case TokenKind::Enum:
    return "'enum'";
  case TokenKind::Class:
    return "'class'";
  case TokenKind::New:
    return "'new'";
  case TokenKind::Delete:
    return "'delete'";
  case TokenKind::Destructor:
    return "'destructor'";
  case TokenKind::Private:
    return "'private'";
  case TokenKind::If:
    return "'if'";
  case TokenKind::Else:
    return "'else'";
  case TokenKind::Match:
    return "'match'";
  case TokenKind::While:
    return "'while'";
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
  case TokenKind::Arrow:
    return "'=>'";
  case TokenKind::EqualEqual:
    return "'=='";
  case TokenKind::Bang:
    return "'!'";
  case TokenKind::BangEqual:
    return "'!='";
  case TokenKind::Plus:
    return "'+'";
  case TokenKind::Minus:
    return "'-'";
  case TokenKind::Star:
    return "'*'";
  case TokenKind::Slash:
    return "'/'";
  case TokenKind::Percent:
    return "'%'";
  case TokenKind::Less:
    return "'<'";
  case TokenKind::LessEqual:
    return "'<='";
  case TokenKind::Greater:
    return "'>'";
  case TokenKind::GreaterEqual:
    return "'>='";
  case TokenKind::AmpAmp:
    return "'&&'";
  case TokenKind::PipePipe:
    return "'||'";
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
