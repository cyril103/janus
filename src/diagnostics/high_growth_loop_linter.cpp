#include "janus/diagnostics/high_growth_loop_linter.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace janus::diagnostics {
namespace {

bool is_identifier(const ast::Expression &expression, std::string_view name) {
  const auto *identifier =
      std::get_if<ast::IdentifierExpression>(&expression.value);
  return identifier != nullptr && identifier->name == name;
}

std::optional<std::int32_t>
integer_literal_value(const ast::Expression &expression) {
  if (const auto *literal =
          std::get_if<ast::IntegerLiteralExpression>(&expression.value))
    return literal->value;

  const auto *unary = std::get_if<ast::UnaryExpression>(&expression.value);
  if (unary == nullptr || unary->operation != ast::UnaryOperator::Negate)
    return std::nullopt;

  if (const auto *literal =
          std::get_if<ast::IntegerLiteralExpression>(&unary->operand->value))
    return -literal->value;
  return std::nullopt;
}

bool has_risky_integer_multiplier(const ast::Expression &expression) {
  const std::optional<std::int32_t> value = integer_literal_value(expression);
  return value.has_value() && (*value > 1 || *value < -1);
}

bool is_self_multiplication(const ast::Expression &expression,
                            std::string_view assigned_name);

bool is_self_multiplicative_growth(const ast::Expression &expression,
                                   std::string_view assigned_name) {
  const auto *binary = std::get_if<ast::BinaryExpression>(&expression.value);
  if (binary == nullptr)
    return false;

  if (is_self_multiplication(expression, assigned_name))
    return true;

  if (binary->operation == ast::BinaryOperator::Add ||
      binary->operation == ast::BinaryOperator::Subtract) {
    return is_self_multiplicative_growth(*binary->left, assigned_name) ||
           is_self_multiplicative_growth(*binary->right, assigned_name);
  }

  return false;
}

bool is_self_multiplication(const ast::Expression &expression,
                            std::string_view assigned_name) {
  const auto *binary = std::get_if<ast::BinaryExpression>(&expression.value);
  if (binary == nullptr ||
      binary->operation != ast::BinaryOperator::Multiply)
    return false;

  if (is_identifier(*binary->left, assigned_name))
    return is_identifier(*binary->right, assigned_name) ||
           has_risky_integer_multiplier(*binary->right);
  if (is_identifier(*binary->right, assigned_name))
    return has_risky_integer_multiplier(*binary->left);
  return false;
}

class Linter {
public:
  void inspect(const ast::Program &program) {
    for (const ast::FunctionDeclaration &function : program.functions)
      inspect_body(function.body, false);
    for (const ast::ClassDeclaration &klass : program.classes) {
      for (const ast::FunctionDeclaration &method : klass.methods)
        inspect_body(method.body, false);
      if (klass.destructor.has_value())
        inspect_body(klass.destructor->body, false);
    }
  }

  [[nodiscard]] const std::vector<HighGrowthLoopWarning> &warnings() const {
    return warnings_;
  }

private:
  void inspect_body(const std::vector<ast::Statement> &body, bool in_loop) {
    for (const ast::Statement &statement : body)
      inspect_statement(statement, in_loop);
  }

  void inspect_statement(const ast::Statement &statement, bool in_loop) {
    if (const auto *assignment =
            std::get_if<ast::AssignmentStatement>(&statement)) {
      if (in_loop && assignment->object.empty() &&
          is_self_multiplicative_growth(assignment->expression,
                                        assignment->name))
        warnings_.push_back(HighGrowthLoopWarning{assignment->location});
      return;
    }

    if (const auto *conditional =
            std::get_if<std::shared_ptr<ast::IfStatement>>(&statement)) {
      inspect_body((*conditional)->then_body, in_loop);
      inspect_body((*conditional)->else_body, in_loop);
      return;
    }

    if (const auto *loop =
            std::get_if<std::shared_ptr<ast::WhileStatement>>(&statement)) {
      inspect_body((*loop)->body, true);
      return;
    }

    if (const auto *loop =
            std::get_if<std::shared_ptr<ast::ForStatement>>(&statement)) {
      inspect_body((*loop)->body, true);
      return;
    }
  }

  std::vector<HighGrowthLoopWarning> warnings_;
};

} // namespace

std::vector<HighGrowthLoopWarning>
find_high_growth_loop_warnings(const ast::Program &program) {
  Linter linter;
  linter.inspect(program);
  return linter.warnings();
}

} // namespace janus::diagnostics
