#include "janus/constant/evaluator.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <bit>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

static_assert(std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<double>::is_iec559,
              "Janus constant evaluation requires IEEE-754 binary32/binary64");

namespace {

using janus::Type;
using janus::TypeKind;
using janus::constant::Value;

const Type *constant_cast_type(std::string_view name) {
  if (name == "int")
    return &Type::int_type();
  if (name == "uint")
    return &Type::uint_type();
  if (name == "long")
    return &Type::long_type();
  if (name == "ulong")
    return &Type::ulong_type();
  if (name == "float")
    return &Type::float_type();
  if (name == "double")
    return &Type::double_type();
  if (name == "byte")
    return &Type::byte_type();
  if (name == "ubyte")
    return &Type::ubyte_type();
  if (name == "short")
    return &Type::short_type();
  if (name == "ushort")
    return &Type::ushort_type();
  if (name == "char")
    return &Type::char_type();
  if (name == "bool")
    return &Type::bool_type();
  if (name == "isize")
    return &Type::isize_type();
  if (name == "usize")
    return &Type::usize_type();
  return nullptr;
}

bool is_policy_cast(std::string_view name) {
  return name == "saturatingCast" || name == "truncatingCast";
}

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
          if (constant_cast_type(node.callee) == nullptr &&
              !is_policy_cast(node.callee))
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

bool is_plan_scalar_constant_expression(
    const janus::ast::Expression &expression,
    const std::unordered_set<std::string> &modules) {
  return std::visit(
      [&](const auto &node) {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (
            std::is_same_v<Node, janus::ast::IntegerLiteralExpression> ||
            std::is_same_v<Node, janus::ast::DoubleLiteralExpression> ||
            std::is_same_v<Node, janus::ast::CharacterLiteralExpression> ||
            std::is_same_v<Node, janus::ast::BooleanLiteralExpression> ||
            std::is_same_v<Node, janus::ast::IdentifierExpression>)
          return true;
        else if constexpr (std::is_same_v<
                               Node,
                               janus::ast::MemberAccessExpression>) {
          const auto module = qualified_name(*node.object);
          return module.has_value() && modules.contains(*module);
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::CallExpression>) {
          return (constant_cast_type(node.callee) != nullptr ||
                  is_policy_cast(node.callee)) &&
                 node.arguments.size() == 1 &&
                 is_plan_scalar_constant_expression(*node.arguments.front(),
                                                    modules);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::UnaryExpression>)
          return is_plan_scalar_constant_expression(*node.operand, modules);
        else if constexpr (std::is_same_v<Node,
                                            janus::ast::BinaryExpression>)
          return is_plan_scalar_constant_expression(*node.left, modules) &&
                 is_plan_scalar_constant_expression(*node.right, modules);
        else
          return false;
      },
      expression.value);
}

bool is_plan_constant_expression(
    const janus::ast::Expression &expression,
    const std::unordered_set<std::string> &modules,
    const std::unordered_set<std::string> &aggregate_types) {
  return std::visit(
      [&](const auto &node) {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (
            std::is_same_v<Node, janus::ast::IntegerLiteralExpression> ||
            std::is_same_v<Node, janus::ast::DoubleLiteralExpression> ||
            std::is_same_v<Node, janus::ast::CharacterLiteralExpression> ||
            std::is_same_v<Node, janus::ast::BooleanLiteralExpression> ||
            std::is_same_v<Node, janus::ast::StringLiteralExpression> ||
            std::is_same_v<Node, janus::ast::IdentifierExpression>)
          return true;
        else if constexpr (std::is_same_v<
                               Node,
                               janus::ast::MemberAccessExpression>) {
          const auto module = qualified_name(*node.object);
          return module.has_value() &&
                 (modules.contains(*module) ||
                  aggregate_types.contains(*module));
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::NewExpression>) {
          return aggregate_types.contains(node.class_name) &&
                 std::all_of(
                     node.arguments.begin(), node.arguments.end(),
                     [&](const auto &argument) {
                       return is_plan_constant_expression(
                           *argument, modules, aggregate_types);
                     });
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::MethodCallExpression>) {
          const auto type_name = qualified_name(*node.object);
          return type_name.has_value() &&
                 aggregate_types.contains(*type_name) &&
                 std::all_of(
                     node.arguments.begin(), node.arguments.end(),
                     [&](const auto &argument) {
                       return is_plan_constant_expression(
                           *argument, modules, aggregate_types);
                     });
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::CallExpression>) {
          return (constant_cast_type(node.callee) != nullptr ||
                  is_policy_cast(node.callee)) &&
                 node.arguments.size() == 1 &&
                 is_plan_scalar_constant_expression(*node.arguments.front(),
                                                    modules);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::UnaryExpression>)
          return is_plan_constant_expression(*node.operand, modules,
                                             aggregate_types);
        else if constexpr (std::is_same_v<Node,
                                            janus::ast::BinaryExpression>)
          return is_plan_constant_expression(*node.left, modules,
                                             aggregate_types) &&
                 is_plan_constant_expression(*node.right, modules,
                                             aggregate_types);
        else if constexpr (std::is_same_v<Node,
                                            janus::ast::IfExpression>)
          return is_plan_constant_expression(*node.condition, modules,
                                             aggregate_types) &&
                 is_plan_constant_expression(*node.then_expression, modules,
                                             aggregate_types) &&
                 is_plan_constant_expression(*node.else_expression, modules,
                                             aggregate_types);
        else if constexpr (std::is_same_v<Node,
                                            janus::ast::MatchExpression>)
          return is_plan_constant_expression(*node.scrutinee, modules,
                                             aggregate_types) &&
                 std::all_of(node.arms.begin(), node.arms.end(),
                             [&](const auto &arm) {
                               return is_plan_constant_expression(
                                   *arm.expression, modules, aggregate_types);
                             });
        else
          return false;
      },
      expression.value);
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

std::uint64_t integer_bits(const Value &value) {
  if (value.type->is_integer())
    return std::get<std::uint64_t>(value.data);
  if (value.type->is_character())
    return static_cast<std::uint32_t>(std::get<char32_t>(value.data));
  if (value.type->kind() == TypeKind::Enum)
    return static_cast<std::uint32_t>(
        std::get<std::shared_ptr<janus::constant::AggregateValue>>(value.data)
            ->tag.value());
  return std::get<bool>(value.data) ? 1 : 0;
}

double floating_value(const Value &value) {
  if (value.type->is_floating_point())
    return std::get<double>(value.data);
  if (value.type->is_integer() && value.type->is_signed())
    return static_cast<double>(
        static_cast<std::int64_t>(std::get<std::uint64_t>(value.data)));
  return static_cast<double>(integer_bits(value));
}

Value cast_value(const Value &source, const Type &destination,
                 janus::SourceLocation location) {
  if (destination.is_boolean()) {
    const bool result = source.type->is_floating_point()
                            ? std::get<double>(source.data) != 0.0
                            : integer_bits(source) != 0;
    return Value{&destination, result};
  }

  if (destination.is_floating_point()) {
    const double converted = floating_value(source);
    const double result = destination.kind() == TypeKind::Float
                              ? static_cast<double>(
                                    static_cast<float>(converted))
                              : converted;
    if (!std::isfinite(result))
      throw janus::CompileError{
          location, "floating constant conversion is not finite"};
    return Value{&destination, result};
  }

  std::uint64_t converted{};
  if (source.type->is_floating_point()) {
    const double value = std::get<double>(source.data);
    if (!std::isfinite(value))
      throw janus::CompileError{
          location, "floating constant conversion is not finite"};
    const long double truncated = std::trunc(static_cast<long double>(value));
    const long double minimum =
        destination.is_signed()
            ? -std::ldexp(static_cast<long double>(1),
                          static_cast<int>(destination.bit_width() - 1))
            : 0.0L;
    const long double maximum =
        destination.is_signed()
            ? std::ldexp(static_cast<long double>(1),
                         static_cast<int>(destination.bit_width() - 1))
            : std::ldexp(static_cast<long double>(1),
                         static_cast<int>(destination.bit_width()));
    if (truncated < minimum || truncated >= maximum)
      throw janus::CompileError{
          location, "floating constant conversion overflows type '" +
                        std::string{destination.name()} + "'"};
    if (destination.is_signed())
      converted = static_cast<std::uint64_t>(
          static_cast<std::int64_t>(truncated));
    else
      converted = static_cast<std::uint64_t>(truncated);
  } else {
    converted = integer_bits(source);
    if (source.type->is_integer() && source.type->is_signed() &&
        source.type->bit_width() < 64) {
      const std::uint64_t sign =
          std::uint64_t{1} << (source.type->bit_width() - 1);
      const std::uint64_t mask =
          (std::uint64_t{1} << source.type->bit_width()) - 1;
      converted &= mask;
      if ((converted & sign) != 0)
        converted |= ~mask;
    }
  }

  if (destination.bit_width() < 64)
    converted &= (std::uint64_t{1} << destination.bit_width()) - 1;
  if (destination.is_character())
    return Value{&destination, static_cast<char32_t>(converted)};
  return Value{&destination, converted};
}

__int128 integer_number(const Value &value) {
  std::uint64_t bits = std::get<std::uint64_t>(value.data);
  if (!value.type->is_signed())
    return static_cast<__int128>(bits);
  const unsigned width = value.type->bit_width();
  if (width < 64) {
    const std::uint64_t sign = std::uint64_t{1} << (width - 1);
    const std::uint64_t mask = (std::uint64_t{1} << width) - 1;
    bits &= mask;
    if ((bits & sign) != 0)
      bits |= ~mask;
  }
  return static_cast<__int128>(static_cast<std::int64_t>(bits));
}

Value policy_cast_value(const Value &source, const Type &destination,
                        bool truncating) {
  if (destination.is_floating_point()) {
    double number = floating_value(source);
    if (std::isnan(number))
      number = 0.0;
    const double maximum = destination.kind() == TypeKind::Float
                               ? std::numeric_limits<float>::max()
                               : std::numeric_limits<double>::max();
    number = std::max(-maximum, std::min(number, maximum));
    if (destination.kind() == TypeKind::Float)
      number = static_cast<double>(static_cast<float>(number));
    return Value{&destination, number};
  }
  const unsigned width = destination.bit_width();
  if (!source.type->is_floating_point()) {
    const __int128 number = integer_number(source);
    if (truncating) {
      std::uint64_t bits = static_cast<std::uint64_t>(number);
      if (width < 64)
        bits &= (std::uint64_t{1} << width) - 1;
      return Value{&destination, bits};
    }
    const __int128 minimum =
        destination.is_signed() ? -(__int128{1} << (width - 1)) : 0;
    const unsigned __int128 unsigned_maximum =
        width == 64 ? std::numeric_limits<std::uint64_t>::max()
                    : (static_cast<unsigned __int128>(1) << width) - 1;
    const __int128 maximum =
        destination.is_signed()
            ? (__int128{1} << (width - 1)) - 1
            : static_cast<__int128>(unsigned_maximum);
    const __int128 clamped = std::max(minimum, std::min(number, maximum));
    return Value{&destination, static_cast<std::uint64_t>(clamped)};
  }
  const double number = std::get<double>(source.data);
  if (std::isnan(number))
    return Value{&destination, std::uint64_t{0}};
  const long double minimum =
      destination.is_signed()
          ? -std::ldexp(static_cast<long double>(1), width - 1)
          : 0.0L;
  const long double upper = std::ldexp(
      static_cast<long double>(1), width - (destination.is_signed() ? 1 : 0));
  long double value = std::trunc(static_cast<long double>(number));
  if (value < minimum)
    value = minimum;
  if (value >= upper)
    value = upper - 1;
  return Value{&destination,
               destination.is_signed()
                   ? static_cast<std::uint64_t>(static_cast<std::int64_t>(value))
                   : static_cast<std::uint64_t>(value)};
}

Value evaluate_impl(const janus::ast::Expression &expression,
                    const Type *expected_type,
                    const janus::constant::Resolver &resolve,
                    const janus::constant::ConstructorResolver
                        &resolve_constructor,
                    const janus::constant::FunctionResolver &call_function);

Value evaluate_binary(const janus::ast::BinaryExpression &binary,
                      const Type *expected_type,
                      const janus::constant::Resolver &resolve,
                      const janus::constant::ConstructorResolver
                          &resolve_constructor,
                      const janus::constant::FunctionResolver &call_function) {
  using janus::ast::BinaryOperator;
  const bool logical = binary.operation == BinaryOperator::LogicalAnd ||
                       binary.operation == BinaryOperator::LogicalOr;
  if (logical) {
    const Value left =
        evaluate_impl(*binary.left, &Type::bool_type(), resolve,
                      resolve_constructor, call_function);
    const bool left_value = std::get<bool>(left.data);
    if (binary.operation == BinaryOperator::LogicalAnd && !left_value)
      return Value{&Type::bool_type(), false};
    if (binary.operation == BinaryOperator::LogicalOr && left_value)
      return Value{&Type::bool_type(), true};
    const Value right =
        evaluate_impl(*binary.right, &Type::bool_type(), resolve,
                      resolve_constructor, call_function);
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
  const Value left = evaluate_impl(*binary.left, operand_hint, resolve,
                                   resolve_constructor, call_function);
  const Value right = evaluate_impl(*binary.right, left.type, resolve,
                                    resolve_constructor, call_function);
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
  if (!signed_type) {
    const unsigned __int128 lhs = unsigned_integer(left);
    const unsigned __int128 rhs = unsigned_integer(right);
    unsigned __int128 value = 0;
    switch (binary.operation) {
    case BinaryOperator::Add:
      value = lhs + rhs;
      break;
    case BinaryOperator::Subtract:
      if (rhs > lhs)
        throw janus::CompileError{
            binary.location, "constant integer expression overflows type '" +
                                 std::string{left.type->name()} + "'"};
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
      value = binary.operation == BinaryOperator::Divide ? lhs / rhs
                                                          : lhs % rhs;
      break;
    default:
      throw janus::CompileError{binary.location,
                                "unsupported integer constant operator"};
    }
    const unsigned width = left.type->bit_width();
    const unsigned __int128 maximum =
        width == 64 ? std::numeric_limits<std::uint64_t>::max()
                    : (static_cast<unsigned __int128>(1) << width) - 1;
    if (value > maximum)
      throw janus::CompileError{
          binary.location, "constant integer expression overflows type '" +
                               std::string{left.type->name()} + "'"};
    return Value{left.type, static_cast<std::uint64_t>(value)};
  }
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
                    const janus::constant::Resolver &resolve,
                    const janus::constant::ConstructorResolver
                        &resolve_constructor,
                    const janus::constant::FunctionResolver &call_function) {
  return std::visit(
      [&](const auto &node) -> Value {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node,
                                     janus::ast::IntegerLiteralExpression>) {
          const Type &type = expected_type != nullptr &&
                                     expected_type->is_integer()
                                 ? *expected_type
                                 : Type::int_type();
          const __int128 magnitude =
              static_cast<__int128>(node.magnitude);
          return integer_value(node.is_negative ? -magnitude : magnitude, type,
                               node.location);
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::DoubleLiteralExpression>) {
          const Type &type =
              !node.is_float && expected_type != nullptr &&
                      expected_type->is_floating_point()
                  ? *expected_type
                  : (node.is_float ? Type::float_type()
                                   : Type::double_type());
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
          if (module.has_value() && resolve_constructor) {
            if (auto shape = resolve_constructor(
                    *module, std::optional<std::string>{node.member}, {},
                    node.location)) {
              auto aggregate = std::make_shared<
                  janus::constant::AggregateValue>();
              aggregate->tag = shape->tag;
              return Value{shape->type, std::move(aggregate)};
            }
          }
          if (module.has_value())
            if (auto value = resolve(module, node.member, node.location))
              return *value;
          throw janus::CompileError{
              node.location,
              "global initializer references non-constant qualified value"};
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::NewExpression>) {
          if (!resolve_constructor)
            throw janus::CompileError{
                node.location,
                "aggregate constructor is not available in constant context"};
          const auto shape =
              resolve_constructor(node.class_name, std::nullopt,
                                  node.type_arguments, node.location);
          if (!shape.has_value() ||
              shape->fields.size() != node.arguments.size())
            throw janus::CompileError{
                node.location,
                "global initializer is not a constant struct constructor"};
          auto aggregate =
              std::make_shared<janus::constant::AggregateValue>();
          aggregate->tag = shape->tag;
          for (std::size_t index = 0; index < node.arguments.size(); ++index)
            aggregate->fields.emplace_back(
                shape->fields[index].first,
                evaluate_impl(*node.arguments[index],
                              shape->fields[index].second, resolve,
                              resolve_constructor, call_function));
          return Value{shape->type, std::move(aggregate)};
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::MethodCallExpression>) {
          const auto type_name = qualified_name(*node.object);
          if (!type_name.has_value() || !resolve_constructor)
            throw janus::CompileError{
                node.location,
                "global initializer is not a constant enum constructor"};
          const auto shape = resolve_constructor(
              *type_name, std::optional<std::string>{node.method},
              node.type_arguments, node.location);
          if (!shape.has_value() ||
              shape->fields.size() != node.arguments.size())
            throw janus::CompileError{
                node.location,
                "global initializer is not a constant enum constructor"};
          auto aggregate =
              std::make_shared<janus::constant::AggregateValue>();
          aggregate->tag = shape->tag;
          for (std::size_t index = 0; index < node.arguments.size(); ++index)
            aggregate->fields.emplace_back(
                shape->fields[index].first,
                evaluate_impl(*node.arguments[index],
                              shape->fields[index].second, resolve,
                              resolve_constructor, call_function));
          return Value{shape->type, std::move(aggregate)};
        } else if constexpr (std::is_same_v<
                                 Node, janus::ast::CallExpression>) {
          const Type *destination = constant_cast_type(node.callee);
          if ((node.callee == "isize" || node.callee == "usize") &&
              expected_type != nullptr && expected_type->name() == node.callee)
            destination = expected_type;
          if (destination == nullptr && is_policy_cast(node.callee))
            destination = expected_type;
          if (destination == nullptr && call_function) {
            std::vector<Value> arguments;
            arguments.reserve(node.arguments.size());
            for (const auto &argument : node.arguments)
              arguments.push_back(evaluate_impl(*argument, nullptr, resolve,
                                                resolve_constructor,
                                                call_function));
            if (auto value = call_function(node.callee, arguments,
                                           node.location))
              return *value;
          }
          if (destination == nullptr || node.arguments.size() != 1)
            throw janus::CompileError{
                node.location,
                "global initializer is not a constant expression"};
          const Value source = evaluate_impl(
              *node.arguments.front(), nullptr, resolve, resolve_constructor,
              call_function);
          if (is_policy_cast(node.callee))
            return policy_cast_value(source, *destination,
                                     node.callee == "truncatingCast");
          return cast_value(source, *destination, node.location);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::IfExpression>) {
          const Value condition = evaluate_impl(
              *node.condition, &Type::bool_type(), resolve,
              resolve_constructor, call_function);
          return evaluate_impl(std::get<bool>(condition.data)
                                   ? *node.then_expression
                                   : *node.else_expression,
                               expected_type, resolve, resolve_constructor,
                               call_function);
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::MatchExpression>) {
          const Value scrutinee = evaluate_impl(
              *node.scrutinee, nullptr, resolve, resolve_constructor,
              call_function);
          if (scrutinee.type->kind() != TypeKind::Enum ||
              !resolve_constructor)
            throw janus::CompileError{
                node.location,
                "constant match currently requires a resource-free enum"};
          const auto &aggregate = *std::get<std::shared_ptr<
              janus::constant::AggregateValue>>(scrutinee.data);
          for (const auto &arm : node.arms) {
            const auto shape = resolve_constructor(
                scrutinee.type->name(), arm.case_name, {}, arm.location);
            if (!shape.has_value() || shape->tag != aggregate.tag)
              continue;
            std::unordered_map<std::string, Value> bindings;
            for (std::size_t index = 0;
                 index < arm.bindings.size() && index < aggregate.fields.size();
                 ++index)
              bindings.emplace(arm.bindings[index],
                               aggregate.fields[index].second);
            const janus::constant::Resolver arm_resolver =
                [&](const std::optional<std::string> &module,
                    std::string_view name, janus::SourceLocation location)
                -> std::optional<Value> {
              if (!module.has_value())
                if (const auto binding = bindings.find(std::string{name});
                    binding != bindings.end())
                  return binding->second;
              return resolve(module, name, location);
            };
            return evaluate_impl(*arm.expression, expected_type, arm_resolver,
                                 resolve_constructor, call_function);
          }
          throw janus::CompileError{node.location,
                                    "constant match is not exhaustive"};
        } else if constexpr (std::is_same_v<Node,
                                            janus::ast::UnaryExpression>) {
          if (node.operation == janus::ast::UnaryOperator::LogicalNot) {
            const Value operand =
                evaluate_impl(*node.operand, &Type::bool_type(), resolve,
                              resolve_constructor, call_function);
            return Value{&Type::bool_type(), !std::get<bool>(operand.data)};
          }
          const Value operand =
              evaluate_impl(*node.operand, expected_type, resolve,
                            resolve_constructor, call_function);
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
          return evaluate_binary(node, expected_type, resolve,
                                 resolve_constructor, call_function);
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

std::string canonical_serialize(const Value &value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << value.type->name() << ':';
  if (const auto *integer = std::get_if<std::uint64_t>(&value.data)) {
    output << "u64:" << std::hex << std::setfill('0') << std::setw(16)
           << *integer;
  } else if (const auto *floating = std::get_if<double>(&value.data)) {
    output << "f" << value.type->bit_width() << ":0x" << std::hex
           << std::setfill('0');
    if (value.type->kind() == TypeKind::Float)
      output << std::setw(8)
             << std::bit_cast<std::uint32_t>(static_cast<float>(*floating));
    else
      output << std::setw(16) << std::bit_cast<std::uint64_t>(*floating);
  } else if (const auto *character = std::get_if<char32_t>(&value.data)) {
    output << "char:" << std::hex << std::setw(8) << std::setfill('0')
           << static_cast<std::uint32_t>(*character);
  } else if (const auto *boolean = std::get_if<bool>(&value.data)) {
    output << "bool:" << (*boolean ? '1' : '0');
  } else if (const auto *text = std::get_if<std::string>(&value.data)) {
    output << "string:" << text->size() << ':' << *text;
  } else {
    const auto &aggregate = **std::get_if<std::shared_ptr<AggregateValue>>(
        &value.data);
    output << "aggregate:tag=";
    if (aggregate.tag)
      output << *aggregate.tag;
    else
      output << '-';
    output << ":fields=" << aggregate.fields.size() << '[';
    for (const auto &[index, field] : aggregate.fields) {
      const std::string serialized = canonical_serialize(field);
      output << index << ':' << serialized.size() << ':' << serialized << ';';
    }
    output << ']';
  }
  return output.str();
}

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
        else if constexpr (std::is_same_v<Node, ast::CallExpression>)
          return (constant_cast_type(node.callee) != nullptr ||
                  is_policy_cast(node.callee)) &&
                 node.arguments.size() == 1 &&
                 is_constant_expression(*node.arguments.front());
        else if constexpr (std::is_same_v<Node, ast::UnaryExpression>)
          return is_constant_expression(*node.operand);
        else if constexpr (std::is_same_v<Node, ast::BinaryExpression>)
          return is_constant_expression(*node.left) &&
                 is_constant_expression(*node.right);
        else if constexpr (std::is_same_v<Node, ast::IfExpression>)
          return is_constant_expression(*node.condition) &&
                 is_constant_expression(*node.then_expression) &&
                 is_constant_expression(*node.else_expression);
        else if constexpr (std::is_same_v<Node, ast::MatchExpression>)
          return is_constant_expression(*node.scrutinee) &&
                 std::all_of(node.arms.begin(), node.arms.end(),
                             [](const auto &arm) {
                               return is_constant_expression(*arm.expression);
                             });
        else
          return false;
      },
      expression.value);
}

InitializationPlan
plan_initialization(const ast::Program &program) {
  const std::vector<ast::GlobalDeclaration> &globals = program.globals;
  std::unordered_map<std::string, const ast::GlobalDeclaration *> by_key;
  std::unordered_map<std::string, std::string> public_by_name;
  std::unordered_set<std::string> modules;
  std::unordered_set<std::string> aggregate_types;
  std::unordered_map<std::string, std::size_t> aggregate_name_counts;
  for (const ast::EnumDeclaration &declaration : program.enums) {
    aggregate_types.insert(
        global_key(declaration.module_name, declaration.name));
    ++aggregate_name_counts[declaration.name];
  }
  for (const ast::ClassDeclaration &declaration : program.classes) {
    if (!declaration.is_value_type)
      continue;
    aggregate_types.insert(
        global_key(declaration.module_name, declaration.name));
    ++aggregate_name_counts[declaration.name];
  }
  for (const auto &[name, count] : aggregate_name_counts)
    if (count == 1)
      aggregate_types.insert(name);
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
  std::vector<std::string> constant_stack;
  std::function<bool(const ast::GlobalDeclaration &)> classify_constant;
  classify_constant = [&](const ast::GlobalDeclaration &global) {
    const std::string key =
        global_key(global.module_name, global.declaration.name);
    if (constant_states[key] == 1) {
      const auto cycle_start =
          std::find(constant_stack.begin(), constant_stack.end(), key);
      std::string chain;
      for (auto iterator = cycle_start; iterator != constant_stack.end();
           ++iterator) {
        if (!chain.empty())
          chain += " -> ";
        chain += *iterator;
      }
      chain += " -> " + key;
      throw CompileError{
          global.declaration.location,
          global.declaration.is_constant
              ? "cyclic constant definition: " + chain
              : "cyclic global constant dependency involving '" + key + "'"};
    }
    if (constant_states[key] == 2)
      return constant_results.at(key);
    if (!global.declaration.is_constant &&
        !is_plan_constant_expression(*global.declaration.initializer, modules,
                                     aggregate_types)) {
      constant_states[key] = 2;
      constant_results.emplace(key, false);
      return false;
    }
    constant_states[key] = 1;
    constant_stack.push_back(key);
    bool result = true;
    for (const auto &[dependency, location] : dependencies(global)) {
      if (dependency->declaration.is_mutable)
        throw CompileError{
            location,
            (global.declaration.is_constant
                 ? "constant '" + global.declaration.name +
                       "' cannot depend on mutable global '"
                 : "global constant initializer cannot depend on mutable "
                   "global '") +
                          global_key(dependency->module_name,
                                     dependency->declaration.name) +
                          "'"};
      if (!classify_constant(*dependency))
        result = false;
    }
    constant_states[key] = 2;
    constant_stack.pop_back();
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
               const Resolver &resolve,
               const ConstructorResolver &resolve_constructor,
               const FunctionResolver &call_function) {
  Value result =
      evaluate_impl(expression, expected_type, resolve, resolve_constructor,
                    call_function);
  if (expected_type != nullptr && result.type->kind() != expected_type->kind())
    throw CompileError{
        std::visit([](const auto &node) { return node.location; },
                   expression.value),
        "constant expression of type '" + std::string{result.type->name()} +
            "' cannot initialize type '" + std::string{expected_type->name()} +
            "'"};
  return result;
}

Value evaluate_statements(
    const std::vector<ast::Statement> &statements, const Type *return_type,
    std::unordered_map<std::string, Value> locals, const Resolver &resolve,
    const ConstructorResolver &resolve_constructor,
    const FunctionResolver &call_function, std::size_t statement_budget) {
  std::size_t steps = 0;
  std::function<std::optional<Value>(const std::vector<ast::Statement> &,
                                     std::unordered_map<std::string, Value>)>
      execute;
  execute = [&](const std::vector<ast::Statement> &body,
                std::unordered_map<std::string, Value> scope)
      -> std::optional<Value> {
    const Resolver scoped_resolver =
        [&](const std::optional<std::string> &module, std::string_view name,
            SourceLocation location) -> std::optional<Value> {
      if (!module.has_value()) {
        const auto local = scope.find(std::string{name});
        if (local != scope.end())
          return local->second;
      }
      return resolve(module, name, location);
    };
    for (const ast::Statement &statement : body) {
      const SourceLocation location = std::visit(
          [](const auto &node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node,
                                         std::shared_ptr<ast::IfStatement>> ||
                          std::is_same_v<Node,
                                         std::shared_ptr<ast::WhileStatement>> ||
                          std::is_same_v<Node,
                                         std::shared_ptr<ast::ForStatement>>)
              return node->location;
            else
              return node.location;
          },
          statement);
      if (++steps > statement_budget)
        throw CompileError{location,
                           "constant evaluation statement budget exceeded (" +
                               std::to_string(statement_budget) + ")"};
      if (const auto *declaration =
              std::get_if<ast::ValueDeclaration>(&statement)) {
        if (!declaration->is_constant || !declaration->initializer ||
            !declaration->declared_type)
          throw CompileError{declaration->location,
                             "const def local declarations must be explicit "
                             "constants"};
        const Type *type = constant_cast_type(declaration->declared_type->name);
        if (type == nullptr)
          throw CompileError{declaration->location,
                             "unsupported local constant type in const def"};
        scope.insert_or_assign(
            declaration->name,
            evaluate(*declaration->initializer, type, scoped_resolver,
                     resolve_constructor, call_function));
      } else if (const auto *returned =
                     std::get_if<ast::ReturnStatement>(&statement)) {
        if (!returned->expression)
          throw CompileError{returned->location,
                             "const def must return a value"};
        return evaluate(*returned->expression, return_type, scoped_resolver,
                        resolve_constructor, call_function);
      } else if (const auto *conditional =
                     std::get_if<std::shared_ptr<ast::IfStatement>>(
                         &statement)) {
        const Value condition =
            evaluate((*conditional)->condition, &Type::bool_type(),
                     scoped_resolver, resolve_constructor, call_function);
        const auto &selected = std::get<bool>(condition.data)
                                   ? (*conditional)->then_body
                                   : (*conditional)->else_body;
        if (auto result = execute(selected, scope))
          return result;
      } else {
        throw CompileError{location,
                           "unsupported statement in const def evaluation"};
      }
    }
    return std::nullopt;
  };
  if (auto result = execute(statements, std::move(locals)))
    return *result;
  throw CompileError{SourceLocation{},
                     "const def completed without returning a value"};
}

} // namespace janus::constant
