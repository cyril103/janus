#pragma once

#include <filesystem>
#include <string>

namespace janus::driver {

struct Manifest {
  std::filesystem::path path;
  std::string name;
  std::string version;
  std::filesystem::path entry;

  [[nodiscard]] std::filesystem::path root() const {
    return path.parent_path();
  }

  [[nodiscard]] std::filesystem::path entry_path() const {
    return root() / entry;
  }
};

[[nodiscard]] Manifest load_manifest(const std::filesystem::path &path);

} // namespace janus::driver
