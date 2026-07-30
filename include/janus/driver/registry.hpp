#pragma once

#include "janus/driver/manifest.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace janus::driver {

struct RegistryLock {
  std::string registry;
  std::string version;
  std::string metadata_sha256;
  std::string archive_sha256;
};

struct ResolvedRegistryPackage {
  std::filesystem::path root;
  RegistryLock lock;
};

struct RegistrySearchResult {
  std::string package;
  std::string latest_version;
  std::string description;
};

[[nodiscard]] std::string registry_location();
[[nodiscard]] bool is_remote_registry(std::string_view location);
[[nodiscard]] std::filesystem::path registry_root();
void publish_package(const Manifest &manifest,
                     const std::string &registry = {});
[[nodiscard]] std::vector<RegistrySearchResult>
search_registry(std::string_view query, const std::string &registry = {});
[[nodiscard]] ResolvedRegistryPackage
resolve_remote_package(const Dependency &dependency,
                       const std::optional<RegistryLock> &locked, bool offline,
                       const std::filesystem::path &cache_root);
void add_dependency(const std::filesystem::path &manifest_path,
                    const Dependency &dependency);
void remove_dependency(const std::filesystem::path &manifest_path,
                       const std::string &name);

} // namespace janus::driver
