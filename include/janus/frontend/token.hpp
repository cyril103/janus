#pragma once

#include "janus/diagnostics/compile_error.hpp"

#include <string_view>

namespace janus::frontend {

enum class TokenKind {
  Module,
  Import,
  As,
  Extern,
  Tailrec,
  Def,
  Type,
  Trait,
  Extends,
  Enum,
  Class,
  Struct,
  Derives,
  New,
  Move,
  Consume,
  Borrow,
  Defer,
  Delete,
  Destructor,
  Private,
  Internal,
  If,
  Else,
  Match,
  For,
  In,
  While,
  Break,
  Continue,
  Return,
  Const,
  Pure,
  StaticAssert,
  Val,
  Var,
  True,
  False,
  DocumentationComment,
  Identifier,
  IntegerLiteral,
  FloatLiteral,
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
  PlusEqual,
  MinusEqual,
  StarEqual,
  SlashEqual,
  PercentEqual,
  AmpersandEqual,
  PipeEqual,
  CaretEqual,
  ShiftLeftEqual,
  ShiftRightEqual,
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
  Ampersand,
  Pipe,
  Caret,
  ShiftLeft,
  ShiftRight,
  AmpAmp,
  PipePipe,
  PipeGreater,
  Semicolon,
  Dot,
  Ellipsis,
  Question,
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
  case TokenKind::As:
    return "'as'";
  case TokenKind::Extern:
    return "'extern'";
  case TokenKind::Tailrec:
    return "'tailrec'";
  case TokenKind::Def:
    return "'def'";
  case TokenKind::Type:
    return "'type'";
  case TokenKind::Trait:
    return "'trait'";
  case TokenKind::Extends:
    return "'extends'";
  case TokenKind::Enum:
    return "'enum'";
  case TokenKind::Class:
    return "'class'";
  case TokenKind::Struct:
    return "'struct'";
  case TokenKind::Derives:
    return "'derives'";
  case TokenKind::New:
    return "'new'";
  case TokenKind::Move:
    return "'move'";
  case TokenKind::Consume:
    return "'consume'";
  case TokenKind::Borrow:
    return "'borrow'";
  case TokenKind::Defer:
    return "'defer'";
  case TokenKind::Delete:
    return "'delete'";
  case TokenKind::Destructor:
    return "'destructor'";
  case TokenKind::Private:
    return "'private'";
  case TokenKind::Internal:
    return "'internal'";
  case TokenKind::If:
    return "'if'";
  case TokenKind::Else:
    return "'else'";
  case TokenKind::Match:
    return "'match'";
  case TokenKind::For:
    return "'for'";
  case TokenKind::In:
    return "'in'";
  case TokenKind::While:
    return "'while'";
  case TokenKind::Break:
    return "'break'";
  case TokenKind::Continue:
    return "'continue'";
  case TokenKind::Return:
    return "'return'";
  case TokenKind::Const:
    return "'const'";
  case TokenKind::Pure:
    return "'pure'";
  case TokenKind::StaticAssert:
    return "'staticAssert'";
  case TokenKind::Val:
    return "'val'";
  case TokenKind::Var:
    return "'var'";
  case TokenKind::True:
    return "'true'";
  case TokenKind::False:
    return "'false'";
  case TokenKind::DocumentationComment:
    return "documentation comment";
  case TokenKind::Identifier:
    return "identifier";
  case TokenKind::IntegerLiteral:
    return "integer literal";
  case TokenKind::FloatLiteral:
    return "float literal";
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
  case TokenKind::PlusEqual:
    return "'+='";
  case TokenKind::MinusEqual:
    return "'-='";
  case TokenKind::StarEqual:
    return "'*='";
  case TokenKind::SlashEqual:
    return "'/='";
  case TokenKind::PercentEqual:
    return "'%='";
  case TokenKind::AmpersandEqual:
    return "'&='";
  case TokenKind::PipeEqual:
    return "'|='";
  case TokenKind::CaretEqual:
    return "'^='";
  case TokenKind::ShiftLeftEqual:
    return "'<<='";
  case TokenKind::ShiftRightEqual:
    return "'>>='";
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
  case TokenKind::Ampersand:
    return "'&'";
  case TokenKind::Pipe:
    return "'|'";
  case TokenKind::Caret:
    return "'^'";
  case TokenKind::ShiftLeft:
    return "'<<'";
  case TokenKind::ShiftRight:
    return "'>>'";
  case TokenKind::AmpAmp:
    return "'&&'";
  case TokenKind::PipePipe:
    return "'||'";
  case TokenKind::PipeGreater:
    return "'|>'";
  case TokenKind::Semicolon:
    return "';'";
  case TokenKind::Dot:
    return "'.'";
  case TokenKind::Ellipsis:
    return "'...'";
  case TokenKind::Question:
    return "'?'";
  case TokenKind::End:
    return "end of file";
  }
  return "token";
}

} // namespace janus::frontend
