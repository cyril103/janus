#pragma once

#include "janus/diagnostics/compile_error.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace janus::diagnostics {

enum class DiagnosticFormat {
  Human,
  Json,
};

struct RenderOptions {
  std::size_t width{80};
};

[[nodiscard]] std::string
render_diagnostics(const std::filesystem::path &path, std::string_view source,
                   std::span<const Diagnostic> diagnostics,
                   DiagnosticFormat format,
                   RenderOptions options = RenderOptions{});

} // namespace janus::diagnostics
