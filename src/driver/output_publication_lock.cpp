#include "janus/driver/output_publication_lock.hpp"

#include "janus/driver/incremental_cache.hpp"

#include <cwctype>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace janus::driver {

OutputPublicationLock::OutputPublicationLock(
    const std::filesystem::path &output) {
#ifdef _WIN32
  std::wstring normalized =
      std::filesystem::absolute(output).lexically_normal().wstring();
  for (wchar_t &character : normalized)
    character = static_cast<wchar_t>(std::towlower(character));
  const std::string digest = stable_digest(
      std::string{reinterpret_cast<const char *>(normalized.data()),
                  normalized.size() * sizeof(wchar_t)});
  const std::wstring name = L"Local\\JanusOutputPublication-" +
                            std::wstring{digest.begin(), digest.end()};
  mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
  if (mutex_ == nullptr)
    throw std::runtime_error{
        "cannot create output publication lock: " +
        std::system_category().message(static_cast<int>(GetLastError()))};
  const DWORD wait = WaitForSingleObject(mutex_, 10000);
  if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
    const DWORD error = wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
    CloseHandle(mutex_);
    mutex_ = nullptr;
    if (error == ERROR_TIMEOUT)
      throw std::runtime_error{"timed out waiting for output publication lock"};
    throw std::runtime_error{
        "cannot acquire output publication lock: " +
        std::system_category().message(static_cast<int>(error))};
  }
#else
  static_cast<void>(output);
#endif
}

OutputPublicationLock::~OutputPublicationLock() {
#ifdef _WIN32
  if (mutex_ != nullptr) {
    ReleaseMutex(mutex_);
    CloseHandle(mutex_);
  }
#endif
}

} // namespace janus::driver
