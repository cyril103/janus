#include <cctype>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string request_id(std::string_view message) {
  const std::size_t key = message.find("\"id\"");
  if (key == std::string_view::npos)
    return {};
  std::size_t cursor = message.find(':', key + 4);
  if (cursor == std::string_view::npos)
    return {};
  do {
    ++cursor;
  } while (cursor < message.size() &&
           std::isspace(static_cast<unsigned char>(message[cursor])));
  if (cursor == message.size())
    return {};
  if (message[cursor] == '"') {
    const std::size_t end = message.find('"', cursor + 1);
    return end == std::string_view::npos
               ? std::string{}
               : std::string{message.substr(cursor, end - cursor + 1)};
  }
  std::size_t end = cursor;
  while (end < message.size() &&
         (std::isalnum(static_cast<unsigned char>(message[end])) ||
          message[end] == '-' || message[end] == '.'))
    ++end;
  return std::string{message.substr(cursor, end - cursor)};
}

void respond(const std::string &body) {
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n"
            << body << std::flush;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::cout << "janus-lsp " << JANUS_VERSION << '\n';
    return 0;
  }

  bool shutdown = false;
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
    const std::string id = request_id(message);
    if (message.find("\"method\":\"initialize\"") != std::string::npos) {
      respond("{\"jsonrpc\":\"2.0\",\"id\":" + id +
              ",\"result\":{\"capabilities\":{\"textDocumentSync\":1,"
              "\"documentFormattingProvider\":true},"
              "\"serverInfo\":{\"name\":\"janus-lsp\",\"version\":\"" +
              JANUS_VERSION + "\"}}}");
    } else if (message.find("\"method\":\"shutdown\"") != std::string::npos) {
      shutdown = true;
      respond("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
    } else if (message.find("\"method\":\"exit\"") != std::string::npos) {
      return shutdown ? 0 : 1;
    } else if (!id.empty()) {
      respond("{\"jsonrpc\":\"2.0\",\"id\":" + id +
              ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
    }
  }
  return 0;
}
