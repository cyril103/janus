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

class Analyzer final {
public:
  [[nodiscard]] SymbolTable analyze(const ast::Program &program) const;

private:
  [[nodiscard]] const Type &
  expression_type(const ast::Expression &expression) const noexcept;
};

} // namespace janus::semantic
