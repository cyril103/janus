#pragma once

#include <filesystem>

namespace janus::driver {

// Serializes the short, atomic publication step for one final build output.
// Cache artifacts remain immutable and independently published; this lock only
// arbitrates replacement of the user-visible output path on Windows.
class OutputPublicationLock final {
public:
  explicit OutputPublicationLock(const std::filesystem::path &output);
  OutputPublicationLock(const OutputPublicationLock &) = delete;
  OutputPublicationLock &operator=(const OutputPublicationLock &) = delete;
  ~OutputPublicationLock();

private:
#ifdef _WIN32
  void *mutex_{nullptr};
#endif
};

} // namespace janus::driver
