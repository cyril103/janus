#pragma once

#include <filesystem>
#include <vector>

namespace janus::driver {

struct LinkOptions {
  bool debug{};
  std::vector<std::filesystem::path> libraries;
};

void link_executable(const std::vector<std::filesystem::path> &objects,
                     const std::filesystem::path &output,
                     const LinkOptions &options = {});

} // namespace janus::driver
