#pragma once

#include "janus/driver/manifest.hpp"

#include <filesystem>
#include <vector>

namespace janus::driver {

struct DependencyOptions {
  bool locked{};
  bool offline{};
};

[[nodiscard]] std::vector<std::filesystem::path>
resolve_dependencies(const Manifest &manifest,
                     const DependencyOptions &options = {});

} // namespace janus::driver
