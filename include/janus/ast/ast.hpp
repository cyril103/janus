#pragma once

#include "janus/diagnostics/compile_error.hpp"
#include "janus/types/type.hpp"

#include <cstdint>
#include <memory>
#include <optional>
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

struct NewExpression {
  std::string class_name;
  std::vector<std::unique_ptr<Expression>> arguments;
  SourceLocation location;
};

struct MemberAccessExpression {
  std::unique_ptr<Expression> object;
  std::string member;
  SourceLocation location;
};

struct MethodCallExpression {
  std::unique_ptr<Expression> object;
  std::string method;
  std::vector<std::unique_ptr<Expression>> arguments;
  SourceLocation location;
};

struct Expression {
  using Value =
      std::variant<IntegerLiteralExpression, DoubleLiteralExpression,
                   CharacterLiteralExpression, BooleanLiteralExpression,
                   StringLiteralExpression, IdentifierExpression,
                   CallExpression, NewExpression, MemberAccessExpression,
                   MethodCallExpression>;

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
  std::optional<Expression> initializer;
  SourceLocation location;
  bool is_private{};
};

struct AssignmentStatement {
  std::string object;
  std::string name;
  Expression expression;
  SourceLocation location;
};

struct DeleteStatement {
  Expression expression;
  SourceLocation location;
};

struct ReturnStatement {
  Expression expression;
  SourceLocation location;
};

using Statement = std::variant<ValueDeclaration, AssignmentStatement,
                               DeleteStatement, ReturnStatement>;

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
  bool is_private{};
};

struct DestructorDeclaration {
  std::vector<Statement> body;
  SourceLocation location;
};

struct ClassDeclaration {
  std::string name;
  std::vector<ValueDeclaration> constructor_fields;
  std::vector<ValueDeclaration> fields;
  std::vector<FunctionDeclaration> methods;
  std::optional<DestructorDeclaration> destructor;
  SourceLocation location;
};

struct Program {
  std::vector<ClassDeclaration> classes;
  std::vector<FunctionDeclaration> functions;
};

} // namespace janus::ast
