#include "janus/lsp/server.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

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

  janus::lsp::Server server;
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
