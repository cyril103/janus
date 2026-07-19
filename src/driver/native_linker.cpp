#include "janus/driver/native_linker.hpp"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#endif

namespace {

#ifndef _WIN32
std::string shell_quote(const std::filesystem::path &path) {
  const std::string value = path.string();
  std::string quoted{"'"};
  for (const char character : value) {
    if (character == '\'')
      quoted += "'\\''";
    else
      quoted += character;
  }
  return quoted + '\'';
}

int command_status(int status) {
  if (status == -1)
    return 1;
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return 1;
}
#endif

#ifdef __APPLE__
std::filesystem::path macos_sdk_path() {
  if (const char *configured = std::getenv("SDKROOT");
      configured != nullptr && *configured != '\0' &&
      std::filesystem::is_directory(configured))
    return configured;

  std::array<char, 4096> buffer{};
  FILE *process =
      popen("/usr/bin/xcrun --sdk macosx --show-sdk-path 2>/dev/null", "r");
  if (process == nullptr)
    throw std::runtime_error{"could not run xcrun to locate the macOS SDK"};
  const std::size_t length =
      std::fread(buffer.data(), sizeof(char), buffer.size() - 1, process);
  const int status = pclose(process);
  std::string path{buffer.data(), length};
  while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
    path.pop_back();
  if (status != 0 || !std::filesystem::is_directory(path))
    throw std::runtime_error{
        "could not locate the macOS SDK; install Xcode Command Line Tools"};
  return path;
}
#endif

} // namespace

namespace janus::driver {

void link_executable(const std::vector<std::filesystem::path> &objects,
                     const std::filesystem::path &output,
                     const LinkOptions &options) {
  if (objects.empty())
    throw std::runtime_error{"native link requires at least one object file"};
  const char *configured_driver = std::getenv("JANUS_CC");
  const std::filesystem::path driver =
      configured_driver != nullptr
          ? std::filesystem::path{configured_driver}
          : (options.driver.empty() ? std::filesystem::path{JANUS_CLANG_PATH}
                                    : options.driver);
#ifdef _WIN32
  std::vector<std::wstring> arguments{driver.wstring(), L"-fuse-ld=lld"};
  if (options.debug)
    arguments.emplace_back(L"-g");
  for (const std::filesystem::path &object : objects)
    arguments.push_back(object.wstring());
  for (const std::filesystem::path &library : options.libraries)
    arguments.push_back(library.wstring());
  arguments.emplace_back(L"-o");
  arguments.push_back(output.wstring());

  std::vector<const wchar_t *> argument_pointers;
  argument_pointers.reserve(arguments.size() + 1);
  for (const std::wstring &argument : arguments)
    argument_pointers.push_back(argument.c_str());
  argument_pointers.push_back(nullptr);
  const std::intptr_t status =
      _wspawnv(_P_WAIT, driver.c_str(), argument_pointers.data());
#else
  std::string command = shell_quote(driver) + " -fuse-ld=lld";
#ifdef __APPLE__
  command += " -isysroot " + shell_quote(macos_sdk_path());
#endif
  const std::filesystem::path bundled_library_directory =
      driver.parent_path().parent_path() / "lib";
  if (std::filesystem::is_directory(bundled_library_directory)) {
#ifdef __APPLE__
    constexpr const char *library_path_variable = "DYLD_LIBRARY_PATH";
#else
    constexpr const char *library_path_variable = "LD_LIBRARY_PATH";
#endif
    std::string library_path = bundled_library_directory.string();
    if (const char *inherited = std::getenv(library_path_variable);
        inherited != nullptr && *inherited != '\0')
      library_path += std::string{":"} + inherited;
    command = std::string{library_path_variable} + "=" +
              shell_quote(std::filesystem::path{library_path}) + " " + command;
  }
  if (options.debug)
    command += " -g";
  for (const std::filesystem::path &object : objects)
    command += " " + shell_quote(object);
  for (const std::filesystem::path &library : options.libraries)
    command += " " + shell_quote(library);
  command += " -o " + shell_quote(output);
  const int status = command_status(std::system(command.c_str()));
#endif
  if (status != 0)
    throw std::runtime_error{"LLD native link failed with status " +
                             std::to_string(status)};
}

} // namespace janus::driver
