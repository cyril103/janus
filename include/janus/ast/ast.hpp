#pragma once

#include "janus/diagnostics/compile_error.hpp"
#include "janus/types/type.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace janus::ast {

struct IntegerLiteralExpression {
  std::int32_t value;
  SourceLocation location;
};

using Expression = std::variant<IntegerLiteralExpression>;

struct ValueDeclaration {
  std::string name;
  const Type *declared_type;
  bool is_mutable;
  Expression initializer;
  SourceLocation location;
};

struct Program {
  std::vector<ValueDeclaration> declarations;
};

} // namespace janus::ast
