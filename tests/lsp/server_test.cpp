#include "janus/lsp/server.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
  janus::lsp::Server server;

  const std::vector<std::string> initialized = server.handle(
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
  assert(initialized.size() == 1);
  assert(initialized.front().find("\"textDocumentSync\"") !=
         std::string::npos);

  const std::vector<std::string> invalid = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///broken.janus","text":"def main() : int { return nope }"}}})");
  assert(invalid.size() == 1);
  assert(invalid.front().find("publishDiagnostics") != std::string::npos);
  assert(invalid.front().find("unknown value") != std::string::npos);

  const std::vector<std::string> valid = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { return 0 }"}]}})");
  assert(valid.size() == 1);
  assert(valid.front().find("\"diagnostics\":[]") != std::string::npos);
}
