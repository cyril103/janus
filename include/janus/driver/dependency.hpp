#pragma once

#include "janus/driver/manifest.hpp"

#include <filesystem>
#include <vector>

namespace janus::driver {

[[nodiscard]] std::vector<std::filesystem::path>
resolve_dependencies(const Manifest &manifest);

} // namespace janus::driver
