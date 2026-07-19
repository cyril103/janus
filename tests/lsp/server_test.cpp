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

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { val answer : int = 42 return answer }"}]}})"));
  const std::vector<std::string> hover = server.handle(
      R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":50}}})");
  assert(hover.size() == 1);
  assert(hover.front().find("val answer : int") != std::string::npos);

  const std::vector<std::string> definition = server.handle(
      R"({"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":50}}})");
  assert(definition.front().find("\"character\":23") != std::string::npos);

  const std::vector<std::string> references = server.handle(
      R"({"jsonrpc":"2.0","id":4,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":50},"context":{"includeDeclaration":true}}})");
  assert(references.front().find("\"uri\":\"file:///broken.janus\"") !=
         std::string::npos);

  const std::vector<std::string> completion = server.handle(
      R"({"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":20}}})");
  assert(completion.front().find("\"label\":\"answer\"") != std::string::npos);
  assert(completion.front().find("\"label\":\"int\"") != std::string::npos);
  assert(completion.front().find("\"label\":\"return\"") !=
         std::string::npos);
}
