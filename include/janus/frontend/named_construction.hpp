#pragma once

#include "janus/frontend/token.hpp"

#include <vector>

namespace janus::frontend {

// Token ranges also work on incomplete editor buffers. Nested expressions are
// opaque here; the parser remains responsible for validating the grammar.
struct NamedConstructionTokens {
  std::size_t type;
  std::size_t open;
  std::size_t close;
  std::vector<std::pair<std::size_t, std::size_t>> fields;
};

inline std::vector<NamedConstructionTokens>
named_constructions(const std::vector<Token> &tokens) {
  std::vector<NamedConstructionTokens> result;
  for (std::size_t start = 0; start + 2 < tokens.size(); ++start) {
    if (tokens[start].kind != TokenKind::New ||
        tokens[start + 1].kind != TokenKind::Identifier)
      continue;
    std::size_t open = start + 2;
    while (open + 1 < tokens.size() && tokens[open].kind == TokenKind::Dot &&
           tokens[open + 1].kind == TokenKind::Identifier)
      open += 2;
    if (open < tokens.size() && tokens[open].kind == TokenKind::LeftBracket) {
      int depth = 0;
      do {
        if (tokens[open].kind == TokenKind::LeftBracket)
          ++depth;
        if (tokens[open].kind == TokenKind::RightBracket)
          --depth;
        ++open;
      } while (open < tokens.size() && depth > 0);
    }
    if (open >= tokens.size() || tokens[open].kind != TokenKind::LeftBrace)
      continue;
    NamedConstructionTokens construction{start + 1, open, tokens.size(), {}};
    std::size_t field = open + 1;
    int depth = 0;
    for (std::size_t index = field; index < tokens.size(); ++index) {
      const auto kind = tokens[index].kind;
      if (depth == 0 &&
          (kind == TokenKind::Comma || kind == TokenKind::RightBrace)) {
        if (field < index && tokens[field].kind == TokenKind::Identifier)
          construction.fields.emplace_back(field, index);
        field = index + 1;
        if (kind == TokenKind::RightBrace) {
          construction.close = index;
          break;
        }
      } else if (kind == TokenKind::LeftBrace ||
                 kind == TokenKind::LeftBracket ||
                 kind == TokenKind::LeftParen) {
        ++depth;
      } else if (kind == TokenKind::RightBrace ||
                 kind == TokenKind::RightBracket ||
                 kind == TokenKind::RightParen) {
        --depth;
      }
    }
    if (construction.close == tokens.size() && field < tokens.size() &&
        tokens[field].kind == TokenKind::Identifier)
      construction.fields.emplace_back(field, tokens.size() - 1);
    result.push_back(std::move(construction));
  }
  return result;
}

} // namespace janus::frontend
