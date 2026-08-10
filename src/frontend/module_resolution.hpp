#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace janus::frontend::detail {

[[nodiscard]] std::filesystem::path resolve_module_import(
    std::string_view module, const std::filesystem::path &project_root,
    const std::vector<std::filesystem::path> &search_paths);

} // namespace janus::frontend::detail
