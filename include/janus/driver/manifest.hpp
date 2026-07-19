#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace janus::driver {

struct Dependency {
  std::string name;
  std::filesystem::path path;
  std::string git;
  std::string revision;
  std::string version_requirement;

  [[nodiscard]] bool is_git() const { return !git.empty(); }
  [[nodiscard]] bool is_registry() const { return git.empty() && path.empty(); }
};

struct Manifest {
  std::filesystem::path path;
  std::string name;
  std::string version;
  std::filesystem::path entry;
  std::vector<Dependency> dependencies;

  [[nodiscard]] std::filesystem::path root() const {
    return path.parent_path();
  }

  [[nodiscard]] std::filesystem::path entry_path() const {
    return root() / entry;
  }
};

[[nodiscard]] Manifest load_manifest(const std::filesystem::path &path);
[[nodiscard]] std::filesystem::path
find_manifest(const std::filesystem::path &start);

} // namespace janus::driver
