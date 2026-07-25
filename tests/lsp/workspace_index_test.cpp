#include "janus/lsp/server.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string file_uri(const std::filesystem::path &path) {
  return "file://" +
         std::filesystem::absolute(path).lexically_normal().generic_string();
}

std::size_t occurrences(const std::string &text, std::string_view needle) {
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = text.find(needle, position)) != std::string::npos) {
    ++count;
    position += needle.size();
  }
  return count;
}

struct TemporaryWorkspace {
  std::filesystem::path path;

  ~TemporaryWorkspace() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

} // namespace

int main() {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryWorkspace workspace{
      std::filesystem::temp_directory_path() /
      ("janus-lsp-workspace-" + std::to_string(suffix))};
  std::filesystem::copy(
      JANUS_LSP_WORKSPACE_FIXTURE, workspace.path,
      std::filesystem::copy_options::recursive);

  janus::lsp::Server server{{std::filesystem::path{JANUS_STDLIB_DIR}}};
  const std::string root_uri = file_uri(workspace.path);
  const std::vector<std::string> initialized = server.handle(
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" +
      root_uri + R"("}})");
  assert(initialized.size() == 1);
  assert(initialized.front().find("\"workspaceSymbolProvider\":true") !=
         std::string::npos);
  const std::vector<std::string> registration = server.handle(
      R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
  assert(registration.size() == 1);
  assert(registration.front().find("workspace/didChangeWatchedFiles") !=
         std::string::npos);
  assert(registration.front().find("**/*.janus") != std::string::npos);
  assert(server.handle(
                   R"({"jsonrpc":"2.0","id":"janus-watch-files","result":null})")
             .empty());

  const janus::lsp::WorkspaceIndexMetrics metrics =
      server.workspace_index_metrics();
  assert(metrics.files == 4);
  assert(metrics.symbols == 6);
  assert(metrics.source_bytes != 0);
  assert(metrics.startup_milliseconds <= 2000);
  assert(metrics.estimated_memory_bytes <= 1024 * 1024);

  const std::string main_uri = file_uri(workspace.path / "src/main.janus");
  const std::vector<std::string> opened = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      main_uri + R"(","text":"import library\n\ndef main() : int {\n    return helper()\n}\n"}}})");
  assert(opened.size() == 1);
  assert(opened.front().find("\"diagnostics\":[]") != std::string::npos);

  const std::vector<std::string> definition = server.handle(
      R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      main_uri + R"("},"position":{"line":3,"character":12}}})");
  assert(definition.front().find("deps/library/src/library.janus") !=
         std::string::npos);

  const std::vector<std::string> references = server.handle(
      R"({"jsonrpc":"2.0","id":3,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"position":{"line":3,"character":12},"context":{"includeDeclaration":true}}})");
  assert(occurrences(references.front(), "\"uri\"") == 3);
  assert(references.front().find("tests/reference.janus") !=
         std::string::npos);
  const std::vector<std::string> uses = server.handle(
      R"({"jsonrpc":"2.0","id":4,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"position":{"line":3,"character":12},"context":{"includeDeclaration":false}}})");
  assert(occurrences(uses.front(), "\"uri\"") == 2);

  const std::vector<std::string> workspace_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":5,"method":"workspace/symbol","params":{"query":""}})");
  assert(workspace_symbols.front().find("workspaceValue") !=
         std::string::npos);
  assert(workspace_symbols.front().find("helper") != std::string::npos);
  assert(workspace_symbols.front().find("hiddenWorkspaceValue") ==
         std::string::npos);
  assert(workspace_symbols.front().find("secretHelper") == std::string::npos);

  const std::string unopened_uri =
      file_uri(workspace.path / "src/unopened.janus");
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      unopened_uri +
      R"(","text":"module unopened\n\nval workspaceValue : int = 42\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":")" +
      unopened_uri +
      R"("},"text":"module unopened\n\nval savedValue : int = 43\n"}})"));
  const std::vector<std::string> saved_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":12,"method":"workspace/symbol","params":{"query":"savedValue"}})");
  assert(saved_symbols.front().find("savedValue") != std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":")" +
      unopened_uri + R"("}}})"));
  const std::vector<std::string> reloaded_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":13,"method":"workspace/symbol","params":{"query":"workspaceValue"}})");
  assert(reloaded_symbols.front().find("workspaceValue") !=
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"contentChanges":[{"text":"def main() : int { return workspaceValue }"}]}})"));
  const std::vector<std::string> unopened_definition = server.handle(
      R"({"jsonrpc":"2.0","id":6,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      main_uri + R"("},"position":{"line":0,"character":30}}})");
  assert(unopened_definition.front().find("src/unopened.janus") !=
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"contentChanges":[{"text":"def main() : int { return secretHelper() }"}]}})"));
  const std::vector<std::string> private_definition = server.handle(
      R"({"jsonrpc":"2.0","id":7,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      main_uri + R"("},"position":{"line":0,"character":30}}})");
  assert(private_definition.front().find("\"result\":null") !=
         std::string::npos);

  const std::filesystem::path created_path =
      workspace.path / "src/created.janus";
  {
    std::ofstream output{created_path};
    output << "module created\n\nval createdValue : int = 1\n";
  }
  const std::string created_uri = file_uri(created_path);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":")" +
      created_uri + R"(","type":1}]}})"));
  const std::vector<std::string> created_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":8,"method":"workspace/symbol","params":{"query":"createdValue"}})");
  assert(created_symbols.front().find("createdValue") != std::string::npos);
  assert(server.workspace_index_metrics().files == 5);

  {
    std::ofstream output{created_path};
    output << "module created\n\nval updatedValue : int = 2\n";
  }
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":")" +
      created_uri + R"(","type":2}]}})"));
  const std::vector<std::string> updated_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":9,"method":"workspace/symbol","params":{"query":"updatedValue"}})");
  assert(updated_symbols.front().find("updatedValue") != std::string::npos);
  assert(updated_symbols.front().find("createdValue") == std::string::npos);

  std::filesystem::remove(created_path);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":")" +
      created_uri + R"(","type":3}]}})"));
  const std::vector<std::string> deleted_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":10,"method":"workspace/symbol","params":{"query":"updatedValue"}})");
  assert(deleted_symbols.front().find("updatedValue") == std::string::npos);
  assert(server.workspace_index_metrics().files == 4);

  const std::vector<std::string> stats = server.handle(
      R"({"jsonrpc":"2.0","id":11,"method":"janus/workspaceIndexStats","params":{}})");
  assert(stats.front().find("\"files\":4") != std::string::npos);
  assert(stats.front().find("\"estimatedMemoryBytes\"") != std::string::npos);
  return 0;
}
