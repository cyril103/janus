#include "janus/driver/output_publication_lock.hpp"

#include "janus/driver/incremental_cache.hpp"

#include <atomic>
#include <chrono>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace janus::driver {

OutputPublicationLock::OutputPublicationLock(
    const std::filesystem::path &output) {
#ifdef _WIN32
  std::wstring normalized;
  const std::filesystem::path absolute =
      std::filesystem::absolute(output).lexically_normal();
  const std::filesystem::path parent = absolute.parent_path();
  HANDLE directory = CreateFileW(
      parent.c_str(), 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (directory != INVALID_HANDLE_VALUE) {
    const DWORD length = GetFinalPathNameByHandleW(
        directory, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_NT);
    if (length != 0) {
      normalized.resize(length);
      const DWORD written = GetFinalPathNameByHandleW(
          directory, normalized.data(), length,
          FILE_NAME_NORMALIZED | VOLUME_NAME_NT);
      if (written != 0 && written < length) {
        normalized.resize(written);
        if (!normalized.empty() && normalized.back() != L'\\')
          normalized.push_back(L'\\');
        normalized += absolute.filename().wstring();
      } else {
        normalized.clear();
      }
    }
    CloseHandle(directory);
  }
  if (normalized.empty()) {
    normalized = std::filesystem::weakly_canonical(parent).wstring();
    if (!normalized.empty() && normalized.back() != L'\\' &&
        normalized.back() != L'/')
      normalized.push_back(L'\\');
    normalized += absolute.filename().wstring();
  }
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

void publish_output(const std::filesystem::path &staged,
                    const std::filesystem::path &output) {
  static std::atomic<std::uint64_t> sequence{};
  if (!output.parent_path().empty())
    std::filesystem::create_directories(output.parent_path());
  std::filesystem::path adjacent;
  for (int attempt = 0; attempt < 100; ++attempt) {
    adjacent = output.string() + ".tmp-" +
               std::to_string(
#ifdef _WIN32
                   static_cast<unsigned long long>(GetCurrentProcessId())) +
#else
                   static_cast<unsigned long long>(::getpid())) +
#endif
               "-" + std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()) +
               "-" + std::to_string(sequence.fetch_add(1));
    std::error_code copy_error;
    if (std::filesystem::copy_file(staged, adjacent,
                                   std::filesystem::copy_options::none,
                                   copy_error))
      break;
    adjacent.clear();
    if (copy_error != std::errc::file_exists)
      throw std::runtime_error{"cannot stage output '" + output.string() +
                               "': " + copy_error.message()};
  }
  if (adjacent.empty())
    throw std::runtime_error{"cannot reserve output staging path for '" +
                             output.string() + "'"};
  try {
    OutputPublicationLock lock{output};
    std::error_code error;
#ifdef _WIN32
    if (!MoveFileExW(adjacent.c_str(), output.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      error = std::error_code{static_cast<int>(GetLastError()),
                              std::system_category()};
#else
    std::filesystem::rename(adjacent, output, error);
#endif
    if (error)
      throw std::runtime_error{"cannot replace output '" + output.string() +
                               "': " + error.message()};
    std::error_code ignored;
    std::filesystem::remove(staged, ignored);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(adjacent, ignored);
    throw;
  }
}

} // namespace janus::driver
