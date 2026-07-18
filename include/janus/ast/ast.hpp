#pragma once

#include "janus/diagnostics/compile_error.hpp"
#include "janus/types/type.hpp"

#include <concepts>
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
  std::vector<TypeReference> type_arguments;
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

struct LambdaExpression {
  struct Parameter {
    std::string name;
    TypeReference type;
    SourceLocation location;
  };

  std::vector<Parameter> parameters;
  std::unique_ptr<Expression> body;
  SourceLocation location;
};

struct CallExpression {
  std::string callee;
  std::vector<TypeReference> type_arguments;
  std::vector<std::unique_ptr<Expression>> arguments;
  SourceLocation location;
};

struct NewExpression {
  std::string class_name;
  std::vector<TypeReference> type_arguments;
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
  std::vector<TypeReference> type_arguments;
  std::vector<std::unique_ptr<Expression>> arguments;
  SourceLocation location;
};

struct IfExpression {
  std::unique_ptr<Expression> condition;
  std::unique_ptr<Expression> then_expression;
  std::unique_ptr<Expression> else_expression;
  SourceLocation location;
};

struct MatchExpression {
  struct Arm {
    std::string case_name;
    std::vector<std::string> bindings;
    std::unique_ptr<Expression> expression;
    SourceLocation location;
  };

  std::unique_ptr<Expression> scrutinee;
  std::vector<Arm> arms;
  SourceLocation location;
};

enum class UnaryOperator {
  Negate,
  LogicalNot,
};

struct UnaryExpression {
  UnaryOperator operation;
  std::unique_ptr<Expression> operand;
  SourceLocation location;
};

enum class BinaryOperator {
  Add,
  Subtract,
  Multiply,
  Divide,
  Remainder,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Equal,
  NotEqual,
  LogicalAnd,
  LogicalOr,
};

struct BinaryExpression {
  BinaryOperator operation;
  std::unique_ptr<Expression> left;
  std::unique_ptr<Expression> right;
  SourceLocation location;
};

struct Expression {
  using Value =
      std::variant<IntegerLiteralExpression, DoubleLiteralExpression,
                   CharacterLiteralExpression, BooleanLiteralExpression,
                   StringLiteralExpression, IdentifierExpression,
                   LambdaExpression, CallExpression, NewExpression,
                   MemberAccessExpression, MethodCallExpression, IfExpression,
                   MatchExpression, UnaryExpression, BinaryExpression>;

  template <typename T>
    requires std::constructible_from<Value, T>
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
  std::optional<Expression> expression;
  SourceLocation location;
};

struct ExpressionStatement {
  Expression expression;
  SourceLocation location;
};

struct IfStatement;
struct WhileStatement;
struct ForStatement;

using Statement =
    std::variant<ValueDeclaration, AssignmentStatement, DeleteStatement,
                 ReturnStatement, ExpressionStatement,
                 std::shared_ptr<IfStatement>, std::shared_ptr<WhileStatement>,
                 std::shared_ptr<ForStatement>>;

struct IfStatement {
  Expression condition;
  std::vector<Statement> then_body;
  std::vector<Statement> else_body;
  SourceLocation location;
};

struct WhileStatement {
  Expression condition;
  std::vector<Statement> body;
  SourceLocation location;
};

struct ForStatement {
  std::string binding;
  Expression iterator;
  std::vector<Statement> body;
  SourceLocation location;
};

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

struct EnumDeclaration {
  struct Case {
    std::string name;
    std::int32_t value;
    std::vector<TypeReference> payload_types;
    SourceLocation location;
  };

  std::string name;
  std::vector<std::string> type_parameters;
  std::vector<Case> cases;
  SourceLocation location;
};

struct ClassDeclaration {
  std::string name;
  std::vector<std::string> type_parameters;
  std::vector<FunctionDeclaration::Parameter> constructor_parameters;
  std::vector<ValueDeclaration> constructor_fields;
  std::vector<ValueDeclaration> fields;
  std::vector<FunctionDeclaration> methods;
  std::optional<DestructorDeclaration> destructor;
  SourceLocation location;
};

struct Program {
  std::optional<std::string> module_name;
  std::vector<std::string> imports;
  std::vector<EnumDeclaration> enums;
  std::vector<ClassDeclaration> classes;
  std::vector<FunctionDeclaration> functions;
};

} // namespace janus::ast
