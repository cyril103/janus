#include "janus/lsp/server.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

void respond(const std::string &body) {
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n"
            << body << std::flush;
}

std::filesystem::path executable_path(const char *argv0) {
#ifdef _WIN32
  std::wstring buffer(32768, L'\0');
  const DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                        static_cast<DWORD>(buffer.size()));
  if (size != 0 && size < buffer.size()) {
    buffer.resize(size);
    return std::filesystem::path{buffer};
  }
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) == 0)
    return std::filesystem::weakly_canonical(buffer.c_str());
#else
  std::error_code error;
  const std::filesystem::path path =
      std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error)
    return path;
#endif
  return std::filesystem::absolute(argv0);
}

std::filesystem::path stdlib_path(const char *argv0) {
  const std::filesystem::path installed =
      executable_path(argv0).parent_path().parent_path() / "share/janus/stdlib";
  return std::filesystem::is_directory(installed)
             ? installed
             : std::filesystem::path{JANUS_STDLIB_DIR};
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::cout << "janus-lsp " << JANUS_VERSION << '\n';
    return 0;
  }

  janus::lsp::Server server{{stdlib_path(argv[0])}};
  while (std::cin) {
    std::size_t content_length = 0;
    std::string header;
    while (std::getline(std::cin, header) && header != "\r" &&
           !header.empty()) {
      if (header.starts_with("Content-Length:"))
        content_length =
            std::stoul(header.substr(std::string{"Content-Length:"}.size()));
    }
    if (content_length == 0)
      break;
    std::string message(content_length, '\0');
    std::cin.read(message.data(), static_cast<std::streamsize>(message.size()));
    for (const std::string &reply : server.handle(message))
      respond(reply);
    if (message.find("\"method\":\"exit\"") != std::string::npos)
      break;
  }
  return 0;
}
