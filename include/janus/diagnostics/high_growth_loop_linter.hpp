#pragma once

#include "janus/ast/ast.hpp"
#include "janus/diagnostics/compile_error.hpp"

#include <vector>

namespace janus::diagnostics {

struct HighGrowthLoopWarning {
  SourceLocation location;
};

[[nodiscard]] std::vector<HighGrowthLoopWarning>
find_high_growth_loop_warnings(const ast::Program &program);

} // namespace janus::diagnostics
