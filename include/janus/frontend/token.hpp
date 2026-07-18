#pragma once

#include "janus/diagnostics/compile_error.hpp"

#include <string_view>

namespace janus::frontend {

enum class TokenKind {
  Val,
  Identifier,
  IntegerLiteral,
  Colon,
  Equal,
  Semicolon,
  End,
};

struct Token {
  TokenKind kind;
  std::string_view lexeme;
  SourceLocation location;
};

[[nodiscard]] constexpr std::string_view token_name(TokenKind kind) noexcept {
  switch (kind) {
  case TokenKind::Val:
    return "'val'";
  case TokenKind::Identifier:
    return "identifier";
  case TokenKind::IntegerLiteral:
    return "integer literal";
  case TokenKind::Colon:
    return "':'";
  case TokenKind::Equal:
    return "'='";
  case TokenKind::Semicolon:
    return "';'";
  case TokenKind::End:
    return "end of file";
  }
  return "token";
}

} // namespace janus::frontend
