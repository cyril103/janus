#pragma once

#include "janus/ast/ast.hpp"
#include "janus/types/type.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace janus::constant {

struct AggregateValue;

struct Value {
  const Type *type;
  std::variant<std::uint64_t, double, char32_t, bool, std::string,
               std::shared_ptr<AggregateValue>>
      data;
};

struct AggregateValue {
  std::optional<std::int32_t> tag;
  std::vector<std::pair<std::size_t, Value>> fields;
};

struct ConstructorShape {
  const Type *type;
  std::optional<std::int32_t> tag;
  std::vector<std::pair<std::size_t, const Type *>> fields;
};

struct InitializationPlan {
  std::vector<const ast::GlobalDeclaration *> constants;
  std::vector<const ast::GlobalDeclaration *> dynamic;
};

using Resolver = std::function<std::optional<Value>(
    const std::optional<std::string> &, std::string_view, SourceLocation)>;
using ConstructorResolver = std::function<std::optional<ConstructorShape>(
    std::string_view, const std::optional<std::string> &,
    const std::vector<ast::TypeReference> &, SourceLocation)>;

[[nodiscard]] bool is_constant_expression(const ast::Expression &expression);

[[nodiscard]] InitializationPlan
plan_initialization(const ast::Program &program);

[[nodiscard]] Value evaluate(const ast::Expression &expression,
                             const Type *expected_type,
                             const Resolver &resolve,
                             const ConstructorResolver &resolve_constructor =
                                 {});

} // namespace janus::constant
