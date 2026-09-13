#pragma once

#include <filesystem>
#include <string_view>

namespace janus::driver {

// Exclusively owns a newly created private file until scope exit. Never opens
// or removes a preexisting path. Writes use the original handle throughout.
class PrivateFile {
public:
  explicit PrivateFile(const std::filesystem::path &path);
  ~PrivateFile();
  PrivateFile(const PrivateFile &) = delete;
  PrivateFile &operator=(const PrivateFile &) = delete;

  void write(std::string_view contents);

private:
  std::filesystem::path path_;
#ifdef _WIN32
  void *handle_;
#else
  int descriptor_;
#endif
};

} // namespace janus::driver
