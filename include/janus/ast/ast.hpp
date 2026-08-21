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
  std::uint64_t magnitude;
  bool is_negative;
  SourceLocation location;
};

struct DoubleLiteralExpression {
  double value;
  bool is_float;
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

struct ArrayLiteralExpression {
  std::vector<std::unique_ptr<Expression>> elements;
  SourceLocation location;
};

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
    std::unique_ptr<Expression> literal;
    bool is_wildcard{false};
    std::unique_ptr<Expression> guard;
    std::unique_ptr<Expression> expression;
    SourceLocation location;
  };

  std::unique_ptr<Expression> scrutinee;
  std::vector<Arm> arms;
  SourceLocation location;
};

struct MoveExpression {
  std::unique_ptr<Expression> operand;
  SourceLocation location;
};

struct TryExpression {
  std::unique_ptr<Expression> operand;
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
  ShiftLeft,
  ShiftRight,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Equal,
  NotEqual,
  BitwiseAnd,
  BitwiseXor,
  BitwiseOr,
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
                   ArrayLiteralExpression, LambdaExpression, CallExpression,
                   NewExpression, MemberAccessExpression, MethodCallExpression,
                   IfExpression, MatchExpression, MoveExpression, TryExpression,
                   UnaryExpression, BinaryExpression>;

  template <typename T>
    requires std::constructible_from<Value, T>
  Expression(T expression) : value{std::move(expression)} {}

  Expression(Expression &&) noexcept = default;
  Expression &operator=(Expression &&) noexcept = default;
  Expression(const Expression &) = delete;
  Expression &operator=(const Expression &) = delete;

  Value value;
};

inline bool is_enum_binding_pattern(const MatchExpression::Arm &arm) {
  if (arm.case_name.empty() || !arm.literal)
    return false;
  const auto *call = std::get_if<CallExpression>(&arm.literal->value);
  if (call == nullptr || call->callee != arm.case_name ||
      call->arguments.size() != arm.bindings.size())
    return false;
  for (std::size_t index = 0; index < call->arguments.size(); ++index) {
    const auto *identifier =
        std::get_if<IdentifierExpression>(&call->arguments[index]->value);
    if (identifier == nullptr || identifier->name != arm.bindings[index])
      return false;
  }
  return true;
}

struct ValueDeclaration {
  std::string name;
  std::optional<TypeReference> declared_type;
  bool is_mutable;
  std::optional<Expression> initializer;
  SourceLocation location;
  bool is_private{};
  bool is_internal{};
  std::string documentation;
  bool is_borrowed{};
  bool is_constant{};
};

struct GlobalDeclaration {
  ValueDeclaration declaration;
  std::optional<std::string> module_name;
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

struct DeferStatement {
  std::variant<DeleteStatement, ExpressionStatement> action;
  SourceLocation location;
};

struct BreakStatement {
  SourceLocation location;
};

struct ContinueStatement {
  SourceLocation location;
};

struct IfStatement;
struct WhileStatement;
struct ForStatement;

using Statement =
    std::variant<ValueDeclaration, AssignmentStatement, DeleteStatement,
                 ReturnStatement, ExpressionStatement, DeferStatement,
                 BreakStatement, ContinueStatement,
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

struct TypeConstraint {
  std::string parameter;
  TypeReference trait;
  SourceLocation location;
};

enum class ParameterOwnership {
  Unspecified,
  Borrow,
  BorrowMutable,
  Consume,
};

enum class ReturnOwnership {
  Unspecified,
  Borrow,
  Owned,
};

struct FunctionDeclaration {
  struct Parameter {
    std::string name;
    TypeReference type;
    SourceLocation location;
    ParameterOwnership ownership{ParameterOwnership::Unspecified};
  };

  std::string name;
  std::vector<std::string> type_parameters;
  std::vector<Parameter> parameters;
  TypeReference return_type;
  std::vector<Statement> body;
  SourceLocation location;
  bool is_private{};
  bool is_consuming{};
  std::vector<TypeConstraint> type_constraints;
  bool is_external{};
  std::optional<std::string> external_symbol;
  bool is_variadic{};
  std::optional<std::string> module_name;
  bool is_internal{};
  std::string documentation;
  ReturnOwnership return_ownership{ReturnOwnership::Unspecified};
  bool is_constant{};
  bool is_borrowing{};
};

struct DestructorDeclaration {
  std::vector<Statement> body;
  SourceLocation location;
  std::string documentation;
};

enum class DerivationKind {
  Copy,
  Equality,
  Hashing,
  Debug,
};

struct Derivation {
  DerivationKind kind;
  SourceLocation location;
};

struct EnumDeclaration {
  struct Case {
    std::string name;
    std::int32_t value;
    std::vector<TypeReference> payload_types;
    SourceLocation location;
    std::string documentation;
  };

  std::string name;
  std::vector<std::string> type_parameters;
  std::vector<Case> cases;
  SourceLocation location;
  bool is_private{};
  std::optional<std::string> module_name;
  std::vector<Derivation> derivations;
  std::string documentation;
};

struct TraitDeclaration {
  std::string name;
  std::vector<std::string> type_parameters;
  std::vector<FunctionDeclaration> methods;
  SourceLocation location;
  std::vector<TypeConstraint> type_constraints;
  bool is_private{};
  std::optional<std::string> module_name;
  std::string documentation;
};

struct ClassDeclaration {
  std::string name;
  std::vector<std::string> type_parameters;
  std::vector<TypeReference> implemented_traits;
  std::vector<FunctionDeclaration::Parameter> constructor_parameters;
  std::vector<ValueDeclaration> constructor_fields;
  std::vector<ValueDeclaration> fields;
  std::vector<FunctionDeclaration> methods;
  std::optional<DestructorDeclaration> destructor;
  SourceLocation location;
  std::vector<TypeConstraint> type_constraints;
  bool is_value_type{};
  bool is_private{};
  std::optional<std::string> module_name;
  std::vector<Derivation> derivations;
  std::string documentation;
};

struct ImportDeclaration {
  struct Symbol {
    std::string name;
    std::optional<std::string> alias;
    SourceLocation location;
  };

  std::string module_name;
  std::optional<std::string> module_alias;
  std::vector<Symbol> symbols;
  SourceLocation location;
  std::optional<std::string> importing_module;

  [[nodiscard]] bool is_qualified() const noexcept {
    return module_alias.has_value();
  }
  [[nodiscard]] bool is_selective() const noexcept { return !symbols.empty(); }
};

struct Program {
  struct StaticAssertion {
    Expression condition;
    std::optional<std::string> message;
    SourceLocation location;
    std::optional<std::string> module_name;
    std::filesystem::path source_path;
  };
  std::optional<std::string> module_name;
  std::vector<ImportDeclaration> imports;
  std::vector<GlobalDeclaration> globals;
  std::vector<TraitDeclaration> traits;
  std::vector<EnumDeclaration> enums;
  std::vector<ClassDeclaration> classes;
  std::vector<FunctionDeclaration> functions;
  std::string documentation;
  std::vector<StaticAssertion> static_assertions;
};

} // namespace janus::ast
