#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace janus::driver {

struct DependencyFingerprintInput {
  std::string name;
  std::string public_interface;
  std::string implementation;
  std::string source;
  std::string import_name;
};

struct BuildFingerprintInput {
  std::string janus_version;
  std::string target;
  std::vector<std::string> options;
  std::string source_path;
  std::string source;
  std::vector<DependencyFingerprintInput> dependencies;
};

[[nodiscard]] std::string stable_digest(std::string_view input);
[[nodiscard]] std::string build_identity(const BuildFingerprintInput &input);
[[nodiscard]] std::string consumer_identity(const BuildFingerprintInput &input);
[[nodiscard]] std::string build_fingerprint(const BuildFingerprintInput &input);
[[nodiscard]] std::string
consumer_fingerprint(const BuildFingerprintInput &input);
[[nodiscard]] std::string
public_interface_fingerprint(std::string_view source);

[[nodiscard]] BuildFingerprintInput inspect_build_inputs(
    const std::filesystem::path &entry,
    const std::vector<std::filesystem::path> &search_paths,
    std::string janus_version, std::string target,
    std::vector<std::string> options);

enum class CacheLookup { Hit, Miss, Corrupt };

class IncrementalCache final {
public:
  explicit IncrementalCache(std::filesystem::path root);

  [[nodiscard]] CacheLookup
  restore(std::string_view key, std::string_view identity,
          const std::filesystem::path &output) const;
  void store(std::string_view key, std::string_view identity,
             const std::filesystem::path &artifact,
             std::string_view consumer_fingerprint) const;

  [[nodiscard]] std::filesystem::path
  entry_path(std::string_view key) const;
  [[nodiscard]] std::filesystem::path
  artifact_path(std::string_view key) const;
  [[nodiscard]] std::filesystem::path
  consumer_path(std::string_view key) const;
  [[nodiscard]] bool restore_consumer(
      std::string_view key, std::string_view identity,
      const std::filesystem::path &output,
      std::vector<std::string> *required_definitions = nullptr) const;
  void store_consumer(
      std::string_view key, std::string_view identity,
      const std::filesystem::path &artifact,
      const std::vector<std::string> &required_definitions) const;
  void invalidate_consumer(std::string_view key) const noexcept;

private:
  std::filesystem::path root_;
};

} // namespace janus::driver
