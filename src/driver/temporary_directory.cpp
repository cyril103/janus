#include "janus/driver/temporary_directory.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace janus::driver {
namespace {

std::uint64_t process_id() noexcept {
#ifdef _WIN32
  return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

bool valid_prefix(std::string_view prefix) {
  return !prefix.empty() && prefix != "." && prefix != ".." &&
         prefix.find('/') == std::string_view::npos &&
         prefix.find('\\') == std::string_view::npos;
}

} // namespace

TemporaryDirectory::TemporaryDirectory(std::filesystem::path path)
    : path_{std::move(path)} {}

TemporaryDirectory TemporaryDirectory::create(std::string_view prefix) {
  if (!valid_prefix(prefix))
    throw std::invalid_argument{"invalid temporary directory prefix"};

  std::error_code error;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path(error);
  if (error)
    throw std::runtime_error{"cannot locate temporary directory: " +
                             error.message()};

  static std::atomic<std::uint64_t> sequence{};
  constexpr std::size_t maximum_attempts = 128;
  for (std::size_t attempt = 0; attempt < maximum_attempts; ++attempt) {
    const auto timestamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
    const std::filesystem::path candidate =
        root / (std::string{prefix} + '-' + std::to_string(process_id()) + '-' +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) +
                '-' + std::to_string(timestamp));

    error.clear();
    if (std::filesystem::create_directory(candidate, error))
      return TemporaryDirectory{candidate};
    if (error && error != std::errc::file_exists)
      throw std::runtime_error{"cannot create temporary directory '" +
                               candidate.string() + "': " + error.message()};
  }

  throw std::runtime_error{"cannot create a unique temporary directory"};
}

TemporaryDirectory::TemporaryDirectory(TemporaryDirectory &&other) noexcept
    : path_{std::exchange(other.path_, {})} {}

TemporaryDirectory &
TemporaryDirectory::operator=(TemporaryDirectory &&other) noexcept {
  if (this != &other) {
    cleanup();
    path_ = std::exchange(other.path_, {});
  }
  return *this;
}

TemporaryDirectory::~TemporaryDirectory() { cleanup(); }

void TemporaryDirectory::cleanup() noexcept {
  if (path_.empty())
    return;
  std::error_code ignored;
  std::filesystem::remove_all(path_, ignored);
  path_.clear();
}

} // namespace janus::driver
