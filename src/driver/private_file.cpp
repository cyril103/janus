#include "private_file.hpp"

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sddl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace janus::driver {

PrivateFile::PrivateFile(const std::filesystem::path &path) : path_{path} {
#ifdef _WIN32
  // A protected DACL grants access only to the owner, without inherited ACEs.
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;;GA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr))
    throw std::runtime_error{"cannot prepare private registry request file"};
  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor,
                                 FALSE};
  handle_ =
      CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                  CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  LocalFree(descriptor);
  if (handle_ == INVALID_HANDLE_VALUE)
#else
  descriptor_ =
      ::open(path_.c_str(),
             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (descriptor_ < 0)
#endif
    throw std::runtime_error{"cannot prepare private registry request file"};
}

PrivateFile::~PrivateFile() {
#ifdef _WIN32
  CloseHandle(handle_);
#else
  ::close(descriptor_);
#endif
  std::error_code ignored;
  std::filesystem::remove(path_, ignored);
}

void PrivateFile::write(std::string_view contents) {
  while (!contents.empty()) {
#ifdef _WIN32
    DWORD written = 0;
    const auto size =
        static_cast<DWORD>(std::min<std::size_t>(contents.size(), 64 * 1024));
    if (!WriteFile(handle_, contents.data(), size, &written, nullptr) ||
        written == 0)
      throw std::runtime_error{"cannot write private registry request file"};
#else
    const auto written =
        ::write(descriptor_, contents.data(),
                std::min<std::size_t>(contents.size(), 64 * 1024));
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      throw std::runtime_error{"cannot write private registry request file"};
#endif
    contents.remove_prefix(static_cast<std::size_t>(written));
  }
}

} // namespace janus::driver
