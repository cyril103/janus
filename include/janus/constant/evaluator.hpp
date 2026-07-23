#pragma once

#include "janus/ast/ast.hpp"
#include "janus/types/type.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace janus::constant {

struct Value {
  const Type *type;
  std::variant<std::uint64_t, double, char32_t, bool, std::string> data;
};

struct InitializationPlan {
  std::vector<const ast::GlobalDeclaration *> constants;
  std::vector<const ast::GlobalDeclaration *> dynamic;
};

using Resolver = std::function<std::optional<Value>(
    const std::optional<std::string> &, std::string_view, SourceLocation)>;

[[nodiscard]] bool is_constant_expression(const ast::Expression &expression);

[[nodiscard]] InitializationPlan
plan_initialization(const std::vector<ast::GlobalDeclaration> &globals);

[[nodiscard]] Value evaluate(const ast::Expression &expression,
                             const Type *expected_type,
                             const Resolver &resolve);

} // namespace janus::constant
