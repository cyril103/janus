#pragma once

#include <filesystem>
#include <string_view>

namespace janus::driver {

class TemporaryDirectory final {
public:
  [[nodiscard]] static TemporaryDirectory create(std::string_view prefix);

  TemporaryDirectory(const TemporaryDirectory &) = delete;
  TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
  TemporaryDirectory(TemporaryDirectory &&other) noexcept;
  TemporaryDirectory &operator=(TemporaryDirectory &&other) noexcept;
  ~TemporaryDirectory();

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  explicit TemporaryDirectory(std::filesystem::path path);
  void cleanup() noexcept;

  std::filesystem::path path_;
};

} // namespace janus::driver
