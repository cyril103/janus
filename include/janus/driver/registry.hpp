#pragma once

#include "janus/driver/manifest.hpp"

#include <filesystem>

namespace janus::driver {

[[nodiscard]] std::filesystem::path registry_root();
void publish_package(const Manifest &manifest);
void add_dependency(const std::filesystem::path &manifest_path,
                    const Dependency &dependency);
void remove_dependency(const std::filesystem::path &manifest_path,
                       const std::string &name);

} // namespace janus::driver
