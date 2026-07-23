#include "janus/lsp/server.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
  janus::lsp::Server server{{std::filesystem::path{JANUS_STDLIB_DIR}}};

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

  const std::vector<std::string> missing_import = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///a/deliberately/long/path/used/to/expose/dangling/diagnostic/messages.janus","text":"import module_that_does_not_exist_anywhere\n\ndef main() : int { return 0 }"}}})");
  assert(missing_import.size() == 1);
  assert(missing_import.front().find(
             "cannot resolve imported module "
             "'module_that_does_not_exist_anywhere'") != std::string::npos);

  const std::vector<std::string> valid = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { return 0 }"}]}})");
  assert(valid.size() == 1);
  assert(valid.front().find("\"diagnostics\":[]") != std::string::npos);

  const std::vector<std::string> imported = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///array.janus","text":"import std.array\n\ndef main() : int {\n    val values : Array[int] = new Array[int](usize(1))\n    return int(values.size())\n}\n"}}})");
  assert(imported.size() == 1);
  assert(imported.front().find("\"diagnostics\":[]") != std::string::npos);

  const std::vector<std::string> module = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///library.janus","text":"module library\n\ndef helper() : int { return 42 }"}}})");
  assert(module.size() == 1);
  assert(module.front().find("\"diagnostics\":[]") != std::string::npos);
  assert(module.front().find("entry point") == std::string::npos);

  const std::vector<std::string> invalid_module = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///invalid-library.janus","text":"module invalid_library\n\ndef helper() : int { return missing }"}}})");
  assert(invalid_module.size() == 1);
  assert(invalid_module.front().find("unknown value") != std::string::npos);

  const std::vector<std::string> closed = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"file:///array.janus"}}})");
  assert(closed.size() == 1);
  assert(closed.front().find("\"uri\":\"file:///array.janus\"") !=
         std::string::npos);
  assert(closed.front().find("\"diagnostics\":[]") != std::string::npos);
  assert(closed.front().find("entry point") == std::string::npos);

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

  const auto assert_null_result = [](const std::vector<std::string> &result) {
    assert(result.size() == 1);
    assert(result.front().find("\"result\":null") != std::string::npos);
    assert(result.front().find("\"error\"") == std::string::npos);
  };
  assert_null_result(server.handle(
      R"({"jsonrpc":"2.0","id":7,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":0}}})"));
  assert_null_result(server.handle(
      R"({"jsonrpc":"2.0","id":8,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":0}}})"));
  assert_null_result(server.handle(
      R"({"jsonrpc":"2.0","id":9,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":0},"context":{"includeDeclaration":true}}})"));

  const std::vector<std::string> completion = server.handle(
      R"({"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":20}}})");
  assert(completion.front().find("\"label\":\"answer\"") != std::string::npos);
  assert(completion.front().find("\"label\":\"int\"") != std::string::npos);
  assert(completion.front().find("\"label\":\"return\"") !=
         std::string::npos);

  const std::vector<std::string> formatting = server.handle(
      R"({"jsonrpc":"2.0","id":6,"method":"textDocument/formatting","params":{"textDocument":{"uri":"file:///broken.janus"},"options":{"tabSize":2,"insertSpaces":true}}})");
  assert(formatting.front().find("\"newText\"") != std::string::npos);
}
