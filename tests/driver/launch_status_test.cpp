#include "launch_status.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition) {
  if (!condition)
    throw std::runtime_error{"launch error transfer failed"};
}
} // namespace

int main() {
  for (const bool partial : {false, true}) {
    std::string received;
    int calls = 0;
    const bool success = janus::driver::write_launch_error(
        42, ENOENT,
        [&](int descriptor, const char *bytes, std::size_t size) -> int {
          require(descriptor == 42);
          ++calls;
          // Interrupt both before and after making progress.
          if (calls == 1 || (partial && calls == 3)) {
            errno = EINTR;
            return -1;
          }
          const auto count = partial ? std::size_t{1} : size;
          received.append(bytes, count);
          return static_cast<int>(count);
        });
    require(success && received.size() == sizeof(int));
    int error{};
    std::memcpy(&error, received.data(), sizeof(error));
    require(error == ENOENT);
  }

  for (const int failure : {0, -1}) {
    for (const bool partial : {false, true}) {
      int calls = 0;
      const bool success = janus::driver::write_launch_error(
          42, EACCES,
          [&](int, const char *, std::size_t) -> int {
            ++calls;
            require(calls <= (partial ? 2 : 1));
            if (partial && calls == 1)
              return 1;
            errno = EIO;
            return failure;
          });
      require(!success && calls == (partial ? 2 : 1));
    }
  }
}
