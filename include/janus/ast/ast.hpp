#pragma once

#include "janus/diagnostics/compile_error.hpp"
#include "janus/types/type.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace janus::ast {

struct TypeReference {
  std::string name;
  SourceLocation location;
};

struct IntegerLiteralExpression {
  std::int32_t value;
  SourceLocation location;
};

struct DoubleLiteralExpression {
  double value;
  SourceLocation location;
};

struct CharacterLiteralExpression {
  char32_t value;
  SourceLocation location;
};

struct BooleanLiteralExpression {
  bool value;
  SourceLocation location;
};

struct StringLiteralExpression {
  std::string value;
  SourceLocation location;
};

struct IdentifierExpression {
  std::string name;
  SourceLocation location;
};

struct Expression;

struct CallExpression {
  std::string callee;
  std::vector<TypeReference> type_arguments;
  std::vector<std::unique_ptr<Expression>> arguments;
  SourceLocation location;
};

struct Expression {
  using Value = std::variant<IntegerLiteralExpression, DoubleLiteralExpression,
                             CharacterLiteralExpression,
                             BooleanLiteralExpression, StringLiteralExpression,
                             IdentifierExpression, CallExpression>;

  template <typename T>
  Expression(T expression) : value{std::move(expression)} {}

  Expression(Expression &&) noexcept = default;
  Expression &operator=(Expression &&) noexcept = default;
  Expression(const Expression &) = delete;
  Expression &operator=(const Expression &) = delete;

  Value value;
};

struct ValueDeclaration {
  std::string name;
  TypeReference declared_type;
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
  struct Parameter {
    std::string name;
    TypeReference type;
    SourceLocation location;
  };

  std::string name;
  std::vector<std::string> type_parameters;
  std::vector<Parameter> parameters;
  TypeReference return_type;
  std::vector<Statement> body;
  SourceLocation location;
};

struct Program {
  std::vector<FunctionDeclaration> functions;
};

} // namespace janus::ast
