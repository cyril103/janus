#pragma once

#include "janus/ast/ast.hpp"

#include <string>
#include <unordered_map>

namespace janus::semantic {

struct Symbol {
  const Type *type;
  bool is_mutable;
};

using SymbolTable = std::unordered_map<std::string, Symbol>;

struct AnalysisResult {
  std::unordered_map<std::string, SymbolTable> functions;
};

class Analyzer final {
public:
  [[nodiscard]] AnalysisResult analyze(const ast::Program &program) const;

private:
  void validate_expression(const ast::Expression &expression,
                           const Type &expected_type,
                           SourceLocation location) const;
  [[nodiscard]] const Type &
  expression_type(const ast::Expression &expression) const noexcept;
};

} // namespace janus::semantic
