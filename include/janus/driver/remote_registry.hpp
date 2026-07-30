#pragma once

#include "janus/driver/manifest.hpp"
#include "janus/driver/registry.hpp"

#include <string>

namespace janus::driver {

void publish_remote_package(const Manifest &manifest,
                            const std::string &registry);

} // namespace janus::driver
