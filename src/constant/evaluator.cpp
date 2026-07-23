#include "janus/constant/evaluator.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <cmath>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace {

using janus::Type;
using janus::TypeKind;
using janus::constant::Value;

std::optional<std::string>
qualified_name(const janus::ast::Expression &expression) {
  if (const auto *identifier =
          std::get_if<janus::ast::IdentifierExpression>(&expression.value))
    return identifier->name;
  if (const auto *member =
          std::get_if<janus::ast::MemberAccessExpression>(&expression.value)) {
    if (auto prefix = qualified_name(*member->object))
      return *prefix + "." + member->member;
  }
  return std::nullopt;
}

struct Reference {
  std::optional<std::string> module;
  std::string name;
  janus::SourceLocation location;
};

void collect_references(const janus::ast::Expression &expression,
                        const std::unordered_set<std::string> &modules,
                        std::vector<Reference> &references) {
  std::visit(
      [&](const auto &node) {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node,
                                     janus::ast::IdentifierExpression>) {
          references.push_back(Reference{std::nullopt, node.name,
                                         node.location});
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::MemberAccessExpression>) {
          const auto module = qualified_name(*node.object);
          if (module.has_value() && modules.contains(*module))
            references.push_back(
                Reference{module, node.member, node.location});
          else
            collect_references(*node.object, modules, references);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::MethodCallExpression>) {
          collect_references(*node.object, modules, references);
          for (const auto &argument : node.arguments)
            collect_references(*argument, modules, references);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::CallExpression>) {
          references.push_back(
              Reference{std::nullopt, node.callee, node.location});
          for (const auto &argument : node.arguments)
            collect_references(*argument, modules, references);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::NewExpression>) {
          for (const auto &argument : node.arguments)
            collect_references(*argument, modules, references);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::IfExpression>) {
          collect_references(*node.condition, modules, references);
          collect_references(*node.then_expression, modules, references);
          collect_references(*node.else_expression, modules, references);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::MatchExpression>) {
          collect_references(*node.scrutinee, modules, references);
          for (const auto &arm : node.arms)
            collect_references(*arm.expression, modules, references);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::MoveExpression> ||
                             std::is_same_v<Node,
                                            janus::ast::TryExpression> ||
                             std::is_same_v<Node,
                                            janus::ast::UnaryExpression>) {
          if constexpr (std::is_same_v<Node,
                                       janus::ast::MoveExpression> ||
                        std::is_same_v<Node,
                                       janus::ast::TryExpression>)
            collect_references(*node.operand, modules, references);
          else
            collect_references(*node.operand, modules, references);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::BinaryExpression>) {
          collect_references(*node.left, modules, references);
          collect_references(*node.right, modules, references);
        }
      },
      expression.value);
}

std::string global_key(const std::optional<std::string> &module,
                       std::string_view name) {
  return module.has_value() ? *module + "." + std::string{name}
                            : std::string{name};
}

bool same_type(const Value &left, const Value &right) {
  return left.type->kind() == right.type->kind();
}

std::int64_t signed_integer(const Value &value) {
  return static_cast<std::int64_t>(std::get<std::uint64_t>(value.data));
}

std::uint64_t unsigned_integer(const Value &value) {
  return std::get<std::uint64_t>(value.data);
}

void require_integer_range(__int128 value, const Type &type,
                           janus::SourceLocation location) {
  const unsigned width = type.bit_width();
  if (type.is_signed()) {
    const __int128 minimum = -(__int128{1} << (width - 1));
    const __int128 maximum = (__int128{1} << (width - 1)) - 1;
    if (value < minimum || value > maximum)
      throw janus::CompileError{
          location, "constant integer expression overflows type '" +
                        std::string{type.name()} + "'"};
    return;
  }
  const unsigned __int128 maximum =
      width == 64 ? std::numeric_limits<std::uint64_t>::max()
                  : (static_cast<unsigned __int128>(1) << width) - 1;
  if (value < 0 || static_cast<unsigned __int128>(value) > maximum)
    throw janus::CompileError{
        location, "constant integer expression overflows type '" +
                      std::string{type.name()} + "'"};
}

Value integer_value(__int128 value, const Type &type,
                    janus::SourceLocation location) {
  require_integer_range(value, type, location);
  return Value{&type, static_cast<std::uint64_t>(value)};
}

Value evaluate_impl(const janus::ast::Expression &expression,
                    const Type *expected_type,
                    const janus::constant::Resolver &resolve);

Value evaluate_binary(const janus::ast::BinaryExpression &binary,
                      const Type *expected_type,
                      const janus::constant::Resolver &resolve) {
  using janus::ast::BinaryOperator;
  const bool logical = binary.operation == BinaryOperator::LogicalAnd ||
                       binary.operation == BinaryOperator::LogicalOr;
  if (logical) {
    const Value left =
        evaluate_impl(*binary.left, &Type::bool_type(), resolve);
    const bool left_value = std::get<bool>(left.data);
    if (binary.operation == BinaryOperator::LogicalAnd && !left_value)
      return Value{&Type::bool_type(), false};
    if (binary.operation == BinaryOperator::LogicalOr && left_value)
      return Value{&Type::bool_type(), true};
    const Value right =
        evaluate_impl(*binary.right, &Type::bool_type(), resolve);
    return Value{&Type::bool_type(), std::get<bool>(right.data)};
  }

  const bool comparison =
      binary.operation == BinaryOperator::Less ||
      binary.operation == BinaryOperator::LessEqual ||
      binary.operation == BinaryOperator::Greater ||
      binary.operation == BinaryOperator::GreaterEqual ||
      binary.operation == BinaryOperator::Equal ||
      binary.operation == BinaryOperator::NotEqual;
  const Type *operand_hint = comparison ? nullptr : expected_type;
  const Value left = evaluate_impl(*binary.left, operand_hint, resolve);
  const Value right = evaluate_impl(*binary.right, left.type, resolve);
  if (!same_type(left, right))
    throw janus::CompileError{
        binary.location,
        "constant binary operands must have the same type"};

  if (comparison) {
    bool result = false;
    if (left.type->is_integer()) {
      if (left.type->is_signed()) {
        const std::int64_t lhs = signed_integer(left);
        const std::int64_t rhs = signed_integer(right);
        switch (binary.operation) {
        case BinaryOperator::Less:
          result = lhs < rhs;
          break;
        case BinaryOperator::LessEqual:
          result = lhs <= rhs;
          break;
        case BinaryOperator::Greater:
          result = lhs > rhs;
          break;
        case BinaryOperator::GreaterEqual:
          result = lhs >= rhs;
          break;
        case BinaryOperator::Equal:
          result = lhs == rhs;
          break;
        case BinaryOperator::NotEqual:
          result = lhs != rhs;
          break;
        default:
          break;
        }
      } else {
        const std::uint64_t lhs = unsigned_integer(left);
        const std::uint64_t rhs = unsigned_integer(right);
        switch (binary.operation) {
        case BinaryOperator::Less:
          result = lhs < rhs;
          break;
        case BinaryOperator::LessEqual:
          result = lhs <= rhs;
          break;
        case BinaryOperator::Greater:
          result = lhs > rhs;
          break;
        case BinaryOperator::GreaterEqual:
          result = lhs >= rhs;
          break;
        case BinaryOperator::Equal:
          result = lhs == rhs;
          break;
        case BinaryOperator::NotEqual:
          result = lhs != rhs;
          break;
        default:
          break;
        }
      }
    } else if (left.type->is_floating_point()) {
      const double lhs = std::get<double>(left.data);
      const double rhs = std::get<double>(right.data);
      switch (binary.operation) {
      case BinaryOperator::Less:
        result = lhs < rhs;
        break;
      case BinaryOperator::LessEqual:
        result = lhs <= rhs;
        break;
      case BinaryOperator::Greater:
        result = lhs > rhs;
        break;
      case BinaryOperator::GreaterEqual:
        result = lhs >= rhs;
        break;
      case BinaryOperator::Equal:
        result = lhs == rhs;
        break;
      case BinaryOperator::NotEqual:
        result = lhs != rhs;
        break;
      default:
        break;
      }
    } else if (left.type->kind() == TypeKind::Bool) {
      const bool lhs = std::get<bool>(left.data);
      const bool rhs = std::get<bool>(right.data);
      result = binary.operation == BinaryOperator::Equal ? lhs == rhs
                                                         : lhs != rhs;
    } else if (left.type->kind() == TypeKind::Char) {
      const char32_t lhs = std::get<char32_t>(left.data);
      const char32_t rhs = std::get<char32_t>(right.data);
      switch (binary.operation) {
      case BinaryOperator::Less:
        result = lhs < rhs;
        break;
      case BinaryOperator::LessEqual:
        result = lhs <= rhs;
        break;
      case BinaryOperator::Greater:
        result = lhs > rhs;
        break;
      case BinaryOperator::GreaterEqual:
        result = lhs >= rhs;
        break;
      case BinaryOperator::Equal:
        result = lhs == rhs;
        break;
      case BinaryOperator::NotEqual:
        result = lhs != rhs;
        break;
      default:
        break;
      }
    } else {
      const std::string &lhs = std::get<std::string>(left.data);
      const std::string &rhs = std::get<std::string>(right.data);
      result = binary.operation == BinaryOperator::Equal ? lhs == rhs
                                                         : lhs != rhs;
    }
    return Value{&Type::bool_type(), result};
  }

  if (left.type->is_floating_point()) {
    const double lhs = std::get<double>(left.data);
    const double rhs = std::get<double>(right.data);
    double value = 0.0;
    switch (binary.operation) {
    case BinaryOperator::Add:
      value = lhs + rhs;
      break;
    case BinaryOperator::Subtract:
      value = lhs - rhs;
      break;
    case BinaryOperator::Multiply:
      value = lhs * rhs;
      break;
    case BinaryOperator::Divide:
      value = lhs / rhs;
      break;
    default:
      throw janus::CompileError{binary.location,
                                "unsupported floating constant operator"};
    }
    if (!std::isfinite(value))
      throw janus::CompileError{binary.location,
                                "floating constant expression is not finite"};
    return Value{left.type, value};
  }

  if (!left.type->is_integer())
    throw janus::CompileError{binary.location,
                              "constant arithmetic requires numeric operands"};
  const bool signed_type = left.type->is_signed();
  const __int128 lhs =
      signed_type ? signed_integer(left) : unsigned_integer(left);
  const __int128 rhs =
      signed_type ? signed_integer(right) : unsigned_integer(right);
  __int128 value = 0;
  switch (binary.operation) {
  case BinaryOperator::Add:
    value = lhs + rhs;
    break;
  case BinaryOperator::Subtract:
    value = lhs - rhs;
    break;
  case BinaryOperator::Multiply:
    value = lhs * rhs;
    break;
  case BinaryOperator::Divide:
  case BinaryOperator::Remainder:
    if (rhs == 0)
      throw janus::CompileError{binary.location,
                                "division by zero in constant expression"};
    value = binary.operation == BinaryOperator::Divide ? lhs / rhs : lhs % rhs;
    break;
  default:
    throw janus::CompileError{binary.location,
                              "unsupported integer constant operator"};
  }
  return integer_value(value, *left.type, binary.location);
}

Value evaluate_impl(const janus::ast::Expression &expression,
                    const Type *expected_type,
                    const janus::constant::Resolver &resolve) {
  return std::visit(
      [&](const auto &node) -> Value {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node,
                                     janus::ast::IntegerLiteralExpression>) {
          const Type &type = expected_type != nullptr &&
                                     expected_type->is_integer()
                                 ? *expected_type
                                 : Type::int_type();
          return integer_value(node.value, type, node.location);
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::DoubleLiteralExpression>) {
          const Type &type =
              expected_type != nullptr && expected_type->is_floating_point()
                  ? *expected_type
                  : Type::double_type();
          return Value{&type, node.value};
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::CharacterLiteralExpression>)
          return Value{&Type::char_type(), node.value};
        else if constexpr (std::is_same_v<
                               Node, janus::ast::BooleanLiteralExpression>)
          return Value{&Type::bool_type(), node.value};
        else if constexpr (std::is_same_v<Node,
                                          janus::ast::StringLiteralExpression>)
          return Value{&Type::string_type(), node.value};
        else if constexpr (std::is_same_v<Node,
                                          janus::ast::IdentifierExpression>) {
          if (auto value = resolve(std::nullopt, node.name, node.location))
            return *value;
          throw janus::CompileError{
              node.location,
              "global initializer references non-constant value '" +
                  node.name + "'"};
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::MemberAccessExpression>) {
          const auto module = qualified_name(*node.object);
          if (module.has_value())
            if (auto value = resolve(module, node.member, node.location))
              return *value;
          throw janus::CompileError{
              node.location,
              "global initializer references non-constant qualified value"};
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::UnaryExpression>) {
          if (node.operation == janus::ast::UnaryOperator::LogicalNot) {
            const Value operand =
                evaluate_impl(*node.operand, &Type::bool_type(), resolve);
            return Value{&Type::bool_type(), !std::get<bool>(operand.data)};
          }
          const Value operand =
              evaluate_impl(*node.operand, expected_type, resolve);
          if (operand.type->is_floating_point())
            return Value{operand.type, -std::get<double>(operand.data)};
          if (!operand.type->is_integer() || !operand.type->is_signed())
            throw janus::CompileError{
                node.location,
                "unary '-' requires a signed numeric constant"};
          return integer_value(-static_cast<__int128>(signed_integer(operand)),
                               *operand.type, node.location);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::BinaryExpression>) {
          return evaluate_binary(node, expected_type, resolve);
        } else {
          throw janus::CompileError{
              node.location,
              "global initializer is not a constant expression"};
        }
      },
      expression.value);
}

} // namespace

namespace janus::constant {

bool is_constant_expression(const ast::Expression &expression) {
  return std::visit(
      [](const auto &node) {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, ast::IntegerLiteralExpression> ||
                      std::is_same_v<Node, ast::DoubleLiteralExpression> ||
                      std::is_same_v<Node, ast::CharacterLiteralExpression> ||
                      std::is_same_v<Node, ast::BooleanLiteralExpression> ||
                      std::is_same_v<Node, ast::StringLiteralExpression> ||
                      std::is_same_v<Node, ast::IdentifierExpression>)
          return true;
        else if constexpr (std::is_same_v<Node, ast::MemberAccessExpression>)
          return qualified_name(*node.object).has_value();
        else if constexpr (std::is_same_v<Node, ast::UnaryExpression>)
          return is_constant_expression(*node.operand);
        else if constexpr (std::is_same_v<Node, ast::BinaryExpression>)
          return is_constant_expression(*node.left) &&
                 is_constant_expression(*node.right);
        else
          return false;
      },
      expression.value);
}

InitializationPlan
plan_initialization(const std::vector<ast::GlobalDeclaration> &globals) {
  std::unordered_map<std::string, const ast::GlobalDeclaration *> by_key;
  std::unordered_map<std::string, std::string> public_by_name;
  std::unordered_set<std::string> modules;
  for (const ast::GlobalDeclaration &global : globals) {
    const std::string key =
        global_key(global.module_name, global.declaration.name);
    by_key.emplace(key, &global);
    if (!global.declaration.is_private)
      public_by_name.emplace(global.declaration.name, key);
    if (global.module_name.has_value())
      modules.insert(*global.module_name);
  }

  const auto dependencies =
      [&](const ast::GlobalDeclaration &global) {
        std::vector<Reference> references;
        collect_references(*global.declaration.initializer, modules,
                           references);
        std::vector<std::pair<const ast::GlobalDeclaration *, SourceLocation>>
            result;
        for (const Reference &reference : references) {
          std::string key;
          if (reference.module.has_value()) {
            key = global_key(reference.module, reference.name);
          } else {
            const std::string local =
                global_key(global.module_name, reference.name);
            if (by_key.contains(local))
              key = local;
            else if (const auto exported =
                         public_by_name.find(reference.name);
                     exported != public_by_name.end())
              key = exported->second;
            else
              continue;
          }
          if (const auto dependency = by_key.find(key);
              dependency != by_key.end())
            result.emplace_back(dependency->second, reference.location);
        }
        return result;
      };

  std::unordered_map<std::string, int> constant_states;
  std::unordered_map<std::string, bool> constant_results;
  std::function<bool(const ast::GlobalDeclaration &)> classify_constant;
  classify_constant = [&](const ast::GlobalDeclaration &global) {
    const std::string key =
        global_key(global.module_name, global.declaration.name);
    if (constant_states[key] == 1)
      throw CompileError{
          global.declaration.location,
          "cyclic global constant dependency involving '" + key + "'"};
    if (constant_states[key] == 2)
      return constant_results.at(key);
    if (!is_constant_expression(*global.declaration.initializer)) {
      constant_states[key] = 2;
      constant_results.emplace(key, false);
      return false;
    }
    constant_states[key] = 1;
    bool result = true;
    for (const auto &[dependency, location] : dependencies(global)) {
      if (dependency->declaration.is_mutable)
        throw CompileError{
            location, "global constant initializer cannot depend on mutable "
                      "global '" +
                          global_key(dependency->module_name,
                                     dependency->declaration.name) +
                          "'"};
      if (!classify_constant(*dependency))
        result = false;
    }
    constant_states[key] = 2;
    constant_results.emplace(key, result);
    return result;
  };

  InitializationPlan plan;
  for (const ast::GlobalDeclaration &global : globals)
    if (classify_constant(global))
      plan.constants.push_back(&global);

  std::unordered_set<std::string> constant_keys;
  for (const ast::GlobalDeclaration *global : plan.constants)
    constant_keys.insert(
        global_key(global->module_name, global->declaration.name));
  std::unordered_map<std::string, int> dynamic_states;
  std::function<void(const ast::GlobalDeclaration &)> visit_dynamic;
  visit_dynamic = [&](const ast::GlobalDeclaration &global) {
    const std::string key =
        global_key(global.module_name, global.declaration.name);
    if (constant_keys.contains(key) || dynamic_states[key] == 2)
      return;
    if (dynamic_states[key] == 1)
      throw CompileError{
          global.declaration.location,
          "cyclic dynamic global dependency involving '" + key + "'"};
    dynamic_states[key] = 1;
    for (const auto &[dependency, location] : dependencies(global)) {
      static_cast<void>(location);
      visit_dynamic(*dependency);
    }
    dynamic_states[key] = 2;
    plan.dynamic.push_back(&global);
  };
  for (const ast::GlobalDeclaration &global : globals)
    visit_dynamic(global);
  return plan;
}

Value evaluate(const ast::Expression &expression, const Type *expected_type,
               const Resolver &resolve) {
  Value result = evaluate_impl(expression, expected_type, resolve);
  if (expected_type != nullptr && result.type->kind() != expected_type->kind())
    throw CompileError{
        std::visit([](const auto &node) { return node.location; },
                   expression.value),
        "constant expression of type '" + std::string{result.type->name()} +
            "' cannot initialize type '" + std::string{expected_type->name()} +
            "'"};
  return result;
}

} // namespace janus::constant
