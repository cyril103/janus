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

struct ReturnStatement {
  Expression expression;
  SourceLocation location;
};

using Statement = std::variant<ValueDeclaration, ReturnStatement>;

struct FunctionDeclaration {
  std::string name;
  const Type *return_type;
  std::vector<Statement> body;
  SourceLocation location;
};

struct Program {
  std::vector<FunctionDeclaration> functions;
};

} // namespace janus::ast
