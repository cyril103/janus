#include "janus/lsp/server.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string file_uri(const std::filesystem::path &path) {
  const std::string normalized =
      std::filesystem::absolute(path).lexically_normal().generic_string();
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result = "file://";
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
  std::filesystem::copy(JANUS_LSP_WORKSPACE_FIXTURE, workspace.path,
                        std::filesystem::copy_options::recursive);

  janus::lsp::Server server{{std::filesystem::path{JANUS_STDLIB_DIR}}};
  const std::string root_uri = file_uri(workspace.path);
  const std::vector<std::string> initialized = server.handle(
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" +
      root_uri + R"("}})");
  assert(initialized.size() == 1);
  assert(initialized.front().find("\"workspaceSymbolProvider\":true") !=
         std::string::npos);
  const std::vector<std::string> registration =
      server.handle(R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
  assert(registration.size() == 2);
  assert(registration.front().find("workspace/didChangeWatchedFiles") !=
         std::string::npos);
  assert(registration.front().find("**/*.janus") != std::string::npos);
  assert(registration.back().find("\"code\":\"JANA0014\"") !=
         std::string::npos);
  assert(registration.back().find("src/unopened.janus") != std::string::npos);
  assert(
      server
          .handle(R"({"jsonrpc":"2.0","id":"janus-watch-files","result":null})")
          .empty());

  const janus::lsp::WorkspaceIndexMetrics metrics =
      server.workspace_index_metrics();
  assert(metrics.files == 4);
  assert(metrics.symbols == 8);
  assert(metrics.source_bytes != 0);
  assert(metrics.startup_milliseconds <= 2000);
  assert(metrics.estimated_memory_bytes <= 1024 * 1024);

  const std::string main_uri = file_uri(workspace.path / "src/main.janus");
  const std::vector<std::string> opened = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      main_uri +
      R"(","text":"import library\n\ndef main() : int {\n    return helper()\n}\n"}}})");
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
  assert(references.front().find("tests/reference.janus") != std::string::npos);
  const std::vector<std::string> uses = server.handle(
      R"({"jsonrpc":"2.0","id":4,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"position":{"line":3,"character":12},"context":{"includeDeclaration":false}}})");
  assert(occurrences(uses.front(), "\"uri\"") == 2);

  const std::vector<std::string> workspace_rename = server.handle(
      R"({"jsonrpc":"2.0","id":14,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"position":{"line":3,"character":12},"newName":"renamedHelper"}})");
  assert(occurrences(workspace_rename.front(),
                     "\"newText\":\"renamedHelper\"") == 3);
  assert(workspace_rename.front().find("deps/library/src/library.janus") !=
         std::string::npos);
  assert(workspace_rename.front().find("tests/reference.janus") !=
         std::string::npos);

  const std::string module_a_uri = file_uri(workspace.path / "src/a.janus");
  const std::string module_b_uri = file_uri(workspace.path / "src/b.janus");
  const std::string qualified_consumer_uri =
      file_uri(workspace.path / "src/qualified-consumer.janus");
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      module_a_uri +
      R"(","text":"module a\n\ndef helper() : int { return 1 }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      module_b_uri +
      R"(","text":"module b\n\ndef helper() : int { return 2 }\ndef renamedQualified() : int { return 3 }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      qualified_consumer_uri +
      R"(","text":"import a\nimport b\n\ndef main() : int { return a.helper() }\n"}}})"));
  const std::vector<std::string> qualified_references = server.handle(
      R"({"jsonrpc":"2.0","id":16,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
      qualified_consumer_uri +
      R"("},"position":{"line":3,"character":29},"context":{"includeDeclaration":true}}})");
  assert(qualified_references.front().find(module_a_uri) != std::string::npos);
  assert(qualified_references.front().find(qualified_consumer_uri) !=
         std::string::npos);
  assert(qualified_references.front().find(module_b_uri) == std::string::npos);
  const std::vector<std::string> qualified_rename = server.handle(
      R"({"jsonrpc":"2.0","id":17,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
      qualified_consumer_uri +
      R"("},"position":{"line":3,"character":29},"newName":"renamedQualified"}})");
  assert(occurrences(qualified_rename.front(),
                     "\"newText\":\"renamedQualified\"") == 2);
  assert(qualified_rename.front().find(module_a_uri) != std::string::npos);
  assert(qualified_rename.front().find(qualified_consumer_uri) !=
         std::string::npos);
  assert(qualified_rename.front().find(module_b_uri) == std::string::npos);

  const std::string alias_consumer_uri =
      file_uri(workspace.path / "src/alias-consumer.janus");
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      alias_consumer_uri +
      R"(","text":"import a as alpha\n\ndef main() : int { return alpha.helper() }\n"}}})"));
  const std::vector<std::string> alias_definition = server.handle(
      R"({"jsonrpc":"2.0","id":18,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      alias_consumer_uri + R"("},"position":{"line":2,"character":34}}})");
  assert(alias_definition.front().find(module_a_uri) != std::string::npos);
  const std::vector<std::string> alias_hover = server.handle(
      R"({"jsonrpc":"2.0","id":19,"method":"textDocument/hover","params":{"textDocument":{"uri":")" +
      alias_consumer_uri + R"("},"position":{"line":2,"character":34}}})");
  assert(alias_hover.front().find("alpha.helper (alias of a.helper)") !=
         std::string::npos);
  const std::vector<std::string> alias_completion = server.handle(
      R"({"jsonrpc":"2.0","id":20,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      alias_consumer_uri + R"("},"position":{"line":2,"character":32}}})");
  assert(alias_completion.front().find("\"label\":\"helper\"") !=
         std::string::npos);

  const std::string selective_consumer_uri =
      file_uri(workspace.path / "src/selective-consumer.janus");
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      selective_consumer_uri +
      R"(","text":"import a.{helper as selected}\n\ndef main() : int { return selected() }\n"}}})"));
  const std::vector<std::string> selective_definition = server.handle(
      R"({"jsonrpc":"2.0","id":21,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      selective_consumer_uri + R"("},"position":{"line":2,"character":28}}})");
  assert(selective_definition.front().find(module_a_uri) != std::string::npos);
  const std::vector<std::string> local_alias_rename = server.handle(
      R"({"jsonrpc":"2.0","id":22,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
      selective_consumer_uri +
      R"("},"position":{"line":2,"character":28},"newName":"localAnswer"}})");
  assert(occurrences(local_alias_rename.front(),
                     "\"newText\":\"localAnswer\"") == 2);
  assert(local_alias_rename.front().find(selective_consumer_uri) !=
         std::string::npos);
  assert(local_alias_rename.front().find(module_a_uri) == std::string::npos);

  const std::string library_uri =
      file_uri(workspace.path / "deps/library/src/library.janus");
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      library_uri +
      R"(","text":"module library\n\nprivate def secretHelper() : int { return 7 }\ndef helper() : int { return 42 }\n"}}})"));
  const std::vector<std::string> private_rename = server.handle(
      R"({"jsonrpc":"2.0","id":15,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
      library_uri +
      R"("},"position":{"line":2,"character":14},"newName":"privateRenamed"}})");
  assert(private_rename.front().find(library_uri) != std::string::npos);
  assert(private_rename.front().find(main_uri) == std::string::npos);

  const std::vector<std::string> workspace_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":5,"method":"workspace/symbol","params":{"query":""}})");
  assert(workspace_symbols.front().find("workspaceValue") != std::string::npos);
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
  assert(reloaded_symbols.front().find("workspaceValue") != std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"contentChanges":[{"text":"import unopened\n\ndef main() : int { return workspaceValue }"}]}})"));
  const std::vector<std::string> unopened_definition = server.handle(
      R"({"jsonrpc":"2.0","id":6,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      main_uri + R"("},"position":{"line":2,"character":30}}})");
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

  const std::string warning_source =
      "module created\n\ndef warningValue() : int {\n"
      "    val unused : int = 1\n    return 0\n}\n";
  {
    std::ofstream output{created_path};
    output << warning_source;
  }
  const std::vector<std::string> warning_opened = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      created_uri +
      R"(","text":"module created\n\ndef warningValue() : int {\n    val unused : int = 1\n    return 0\n}\n"}}})");
  assert(warning_opened.front().find("\"code\":\"JANA0014\"") !=
         std::string::npos);
  const std::vector<std::string> warning_closed = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":")" +
      created_uri + R"("}}})");
  assert(warning_closed.front().find("\"code\":\"JANA0014\"") !=
         std::string::npos);

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

  TemporaryWorkspace encoded_workspace{
      std::filesystem::temp_directory_path() /
      ("janus project #80 % \xC3\xA9-" + std::to_string(suffix))};
  std::filesystem::create_directories(encoded_workspace.path / "src");
  const std::filesystem::path encoded_file =
      encoded_workspace.path / "src/module # %.janus";
  {
    std::ofstream output{encoded_file};
    output << "module encoded\n\nval encodedValue : int = 1\n";
  }
  janus::lsp::Server encoded_server;
  const std::string encoded_root_uri = file_uri(encoded_workspace.path);
  const std::string encoded_file_uri = file_uri(encoded_file);
  static_cast<void>(encoded_server.handle(
      R"({"jsonrpc":"2.0","id":40,"method":"initialize","params":{"rootUri":")" +
      encoded_root_uri + R"("}})"));
  const std::vector<std::string> encoded_symbols = encoded_server.handle(
      R"({"jsonrpc":"2.0","id":41,"method":"workspace/symbol","params":{"query":"encodedValue"}})");
  assert(encoded_symbols.front().find(encoded_file_uri) != std::string::npos);
  assert(encoded_symbols.front().find("module # %.janus") == std::string::npos);
  return 0;
}
