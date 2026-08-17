#include "janus/build_identity.hpp"
#include "janus/lsp/server.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

void respond(const std::string &body) {
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n"
            << body << std::flush;
}

std::filesystem::path stdlib_path(const char *argv0);
std::filesystem::path stdlib_api_index_path(const char *argv0);

std::string json_string(std::string_view value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result{"\""};
  for (const unsigned char character : value) {
    switch (character) {
    case '\"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (character < 0x20U) {
        result += "\\u00";
        result.push_back(hex[character >> 4U]);
        result.push_back(hex[character & 0x0FU]);
      } else {
        result.push_back(static_cast<char>(character));
      }
    }
  }
  result.push_back('\"');
  return result;
}

std::string file_uri(const std::filesystem::path &path) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::error_code error;
  std::string normalized =
      std::filesystem::weakly_canonical(std::filesystem::absolute(path), error)
          .generic_string();
  if (error)
    normalized =
        std::filesystem::absolute(path).lexically_normal().generic_string();
#ifdef _WIN32
  if (normalized.starts_with("//"))
    normalized.erase(0, 2);
  else if (normalized.size() >= 2 && normalized[1] == ':')
    normalized.insert(normalized.begin(), '/');
#endif
  std::string result{"file://"};
  for (const unsigned char character : normalized) {
    const bool unreserved = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z') ||
                            (character >= '0' && character <= '9') ||
                            character == '-' || character == '.' ||
                            character == '_' || character == '~' ||
                            character == '/' || character == ':';
    if (unreserved) {
      result.push_back(static_cast<char>(character));
    } else {
      result.push_back('%');
      result.push_back(hex[character >> 4U]);
      result.push_back(hex[character & 0x0FU]);
    }
  }
  return result;
}

int complete_once(int argc, char **argv) {
  if (argc != 7) {
    std::cerr << "usage: janus-lsp --completion <workspace> <document> "
                 "<snapshot> <line> <character>\n";
    return 2;
  }
  std::ifstream input{argv[4], std::ios::binary};
  if (!input) {
    std::cerr << "janus-lsp: cannot read completion snapshot\n";
    return 2;
  }
  const std::string source{std::istreambuf_iterator<char>{input},
                           std::istreambuf_iterator<char>{}};
  std::uint64_t line = 0;
  std::uint64_t character = 0;
  try {
    line = std::stoull(argv[5]);
    character = std::stoull(argv[6]);
  } catch (const std::exception &) {
    std::cerr << "janus-lsp: invalid completion position\n";
    return 2;
  }

  janus::lsp::Server server{{stdlib_path(argv[0])},
                            {stdlib_api_index_path(argv[0])}};
  const std::string document_uri = file_uri(argv[3]);
  const std::string initialize =
      std::string_view{argv[2]} == "-"
          ? "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
            "\"params\":{}}"
          : "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
            "\"params\":{\"rootUri\":" +
                json_string(file_uri(argv[2])) + "}}";
  static_cast<void>(server.handle(initialize));
  static_cast<void>(
      server.handle("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
                    "\"params\":{\"textDocument\":{\"uri\":" +
                    json_string(document_uri) +
                    ",\"languageId\":\"janus\","
                    "\"version\":1,\"text\":" +
                    json_string(source) + "}}}"));
  const std::vector<std::string> response =
      server.handle("{\"jsonrpc\":\"2.0\",\"id\":2,"
                    "\"method\":\"textDocument/completion\",\"params\":{"
                    "\"textDocument\":{\"uri\":" +
                    json_string(document_uri) +
                    "},\"position\":{\"line\":" + std::to_string(line) +
                    ",\"character\":" + std::to_string(character) + "}}}");
  if (response.empty())
    return 1;
  std::cout << response.front() << '\n';
  return 0;
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

std::filesystem::path stdlib_api_index_path(const char *argv0) {
  const std::filesystem::path installed =
      executable_path(argv0).parent_path().parent_path() /
      "share/doc/janus/stdlib-reference/api-index.json";
  return std::filesystem::is_regular_file(installed)
             ? installed
             : std::filesystem::path{JANUS_STDLIB_API_INDEX};
}

} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  static_cast<void>(_setmode(_fileno(stdin), _O_BINARY));
  static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::cout << "janus-lsp " << janus::build::display_version << '\n';
    return 0;
  }
  if (argc == 3 && std::string_view{argv[1]} == "--version" &&
      std::string_view{argv[2]} == "--json") {
    std::cout << janus::build::json() << '\n';
    return 0;
  }
  if (argc >= 2 && std::string_view{argv[1]} == "--completion")
    return complete_once(argc, argv);

  janus::lsp::Server server{{stdlib_path(argv[0])},
                            {stdlib_api_index_path(argv[0])}};
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
