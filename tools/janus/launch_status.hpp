#pragma once

#include <cerrno>
#include <cstddef>

namespace janus::driver {

// Called after fork: use only the supplied write syscall, without allocation.
// The caller must terminate the child even if its error pipe is unusable.
template <typename Write>
bool write_launch_error(int descriptor, int error, Write write) noexcept {
  const auto *bytes = reinterpret_cast<const char *>(&error);
  std::size_t remaining = sizeof(error);
  while (remaining != 0) {
    const auto count = write(descriptor, bytes, remaining);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (count == 0)
      return false;
    bytes += count;
    remaining -= static_cast<std::size_t>(count);
  }
  return true;
}

} // namespace janus::driver
