#include "janus/driver/native_linker.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

std::string shell_quote(const std::filesystem::path &path) {
  const std::string value = path.string();
#ifdef _WIN32
  std::string quoted{"\""};
  for (const char character : value) {
    if (character == '"')
      quoted += '\\';
    quoted += character;
  }
  return quoted + '"';
#else
  std::string quoted{"'"};
  for (const char character : value) {
    if (character == '\'')
      quoted += "'\\''";
    else
      quoted += character;
  }
  return quoted + '\'';
#endif
}

int command_status(int status) {
  if (status == -1)
    return 1;
#ifdef _WIN32
  return status;
#else
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return 1;
#endif
}

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
  std::string command = shell_quote(driver) + " -fuse-ld=lld";
  if (options.debug)
    command += " -g";
  for (const std::filesystem::path &object : objects)
    command += " " + shell_quote(object);
  for (const std::filesystem::path &library : options.libraries)
    command += " " + shell_quote(library);
  command += " -o " + shell_quote(output);
  const int status = command_status(std::system(command.c_str()));
  if (status != 0)
    throw std::runtime_error{"LLD native link failed with status " +
                             std::to_string(status)};
}

} // namespace janus::driver
