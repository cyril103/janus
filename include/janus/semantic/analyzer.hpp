#pragma once

#include "janus/ast/ast.hpp"

#include <string>
#include <unordered_map>

namespace janus::semantic {

struct SemanticType {
  const Type *concrete{};
  std::string parameter;

  [[nodiscard]] bool is_concrete() const noexcept {
    return concrete != nullptr;
  }
  [[nodiscard]] std::string name() const {
    return is_concrete() ? std::string{concrete->name()} : parameter;
  }
};

struct Symbol {
  SemanticType type;
  bool is_mutable;
};

using SymbolTable = std::unordered_map<std::string, Symbol>;

struct AnalysisResult {
  std::unordered_map<std::string, SymbolTable> functions;
};

class Analyzer final {
public:
  [[nodiscard]] AnalysisResult analyze(const ast::Program &program) const;
};

} // namespace janus::semantic
