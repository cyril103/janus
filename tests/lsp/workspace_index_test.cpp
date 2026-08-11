#include "janus/lsp/server.hpp"
#include "janus/driver/api_index.hpp"
#include "janus/frontend/module_loader.hpp"

#include "../support/require.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string file_uri(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::path normalized_path = std::filesystem::weakly_canonical(
      std::filesystem::absolute(path), error);
  if (error)
    normalized_path = std::filesystem::absolute(path).lexically_normal();
  std::string normalized = normalized_path.generic_string();
#ifdef _WIN32
  if (normalized.starts_with("//"))
    normalized.erase(0, 2);
  else if (normalized.size() >= 2 && normalized[1] == ':')
    normalized.insert(normalized.begin(), '/');
#endif
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

bool named_items_have_no_edits(const std::string &json, std::string_view label) {
  const std::string marker = "\"label\":\"" + std::string{label} + "\"";
  std::size_t position = 0;
  bool found = false;
  while ((position = json.find(marker, position)) != std::string::npos) {
    found = true;
    const std::size_t start = json.rfind('{', position);
    if (start == std::string::npos ||
        json.substr(start, position - start).find("additionalTextEdits") !=
            std::string::npos)
      return false;
    position += marker.size();
  }
  return found;
}

struct TemporaryWorkspace {
  std::filesystem::path path;

  ~TemporaryWorkspace() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--verify-require-failure")
    JANUS_REQUIRE(false);

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
  JANUS_REQUIRE(initialized.size() == 1);
  JANUS_REQUIRE(initialized.front().find("\"workspaceSymbolProvider\":true") !=
                std::string::npos);
  const std::vector<std::string> registration =
      server.handle(R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
  JANUS_REQUIRE(registration.size() == 2);
  JANUS_REQUIRE(registration.front().find("workspace/didChangeWatchedFiles") !=
                std::string::npos);
  JANUS_REQUIRE(registration.front().find("**/*.janus") != std::string::npos);
  JANUS_REQUIRE(registration.back().find("\"code\":\"JANA0014\"") !=
                std::string::npos);
  JANUS_REQUIRE(registration.back().find("src/unopened.janus") !=
                std::string::npos);
  JANUS_REQUIRE(
      server
          .handle(R"({"jsonrpc":"2.0","id":"janus-watch-files","result":null})")
          .empty());

  const janus::lsp::WorkspaceIndexMetrics metrics =
      server.workspace_index_metrics();
  JANUS_REQUIRE(metrics.files == 4);
  JANUS_REQUIRE(metrics.symbols == 8);
  JANUS_REQUIRE(metrics.source_bytes != 0);
  JANUS_REQUIRE(metrics.startup_milliseconds <= 2000);
  JANUS_REQUIRE(metrics.estimated_memory_bytes <= 1024 * 1024);

  const std::string main_uri = file_uri(workspace.path / "src/main.janus");
  const std::vector<std::string> opened = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      main_uri +
      R"(","text":"import library\n\ndef main() : int {\n    return helper()\n}\n"}}})");
  JANUS_REQUIRE(opened.size() == 1);
  JANUS_REQUIRE(opened.front().find("\"diagnostics\":[]") != std::string::npos);
  const std::vector<std::string> discovery_completion = server.handle(
      R"({"jsonrpc":"2.0","id":30,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      main_uri + R"("},"position":{"line":3,"character":11}}})");
  JANUS_REQUIRE(discovery_completion.front().find(
                    "\"label\":\"workspaceValue\"") != std::string::npos);
  JANUS_REQUIRE(discovery_completion.front().find(
                    "\"additionalTextEdits\"") != std::string::npos);
  JANUS_REQUIRE(discovery_completion.front().find(
                    "import unopened\\n") != std::string::npos);

  const std::vector<std::string> definition = server.handle(
      R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      main_uri + R"("},"position":{"line":3,"character":12}}})");
  JANUS_REQUIRE(definition.front().find("deps/library/src/library.janus") !=
                std::string::npos);

  const std::vector<std::string> references = server.handle(
      R"({"jsonrpc":"2.0","id":3,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"position":{"line":3,"character":12},"context":{"includeDeclaration":true}}})");
  JANUS_REQUIRE(occurrences(references.front(), "\"uri\"") == 3);
  JANUS_REQUIRE(references.front().find("tests/reference.janus") !=
                std::string::npos);
  const std::vector<std::string> uses = server.handle(
      R"({"jsonrpc":"2.0","id":4,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"position":{"line":3,"character":12},"context":{"includeDeclaration":false}}})");
  JANUS_REQUIRE(occurrences(uses.front(), "\"uri\"") == 2);

  const std::vector<std::string> workspace_rename = server.handle(
      R"({"jsonrpc":"2.0","id":14,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"position":{"line":3,"character":12},"newName":"renamedHelper"}})");
  JANUS_REQUIRE(occurrences(workspace_rename.front(),
                            "\"newText\":\"renamedHelper\"") == 3);
  JANUS_REQUIRE(workspace_rename.front().find(
                    "deps/library/src/library.janus") != std::string::npos);
  JANUS_REQUIRE(workspace_rename.front().find("tests/reference.janus") !=
                std::string::npos);

  const std::string module_a_uri = file_uri(workspace.path / "src/a.janus");
  const std::string module_b_uri = file_uri(workspace.path / "src/b.janus");
  const std::string qualified_consumer_uri =
      file_uri(workspace.path / "src/qualified-consumer.janus");
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      module_a_uri +
      R"(","text":"module a\n\ndef helper() : int { return 1 }\ndef uniqueA() : int { return 4 }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      module_b_uri +
      R"(","text":"module b\n\nclass Widget() {}\ntrait Printable {}\nenum Status { Ready }\ndef helper() : int { return 2 }\ndef renamedQualified() : int { return 3 }\ndef intMaker() : int { return 4 }\ndef stringMaker() : string { return \"x\" }\ndef unary(value : int) : int { return value }\ndef binary(left : int, right : int) : int { return left + right }\ndef zGeneric[T](value : T) : int { return 1 }\ndef aPair[T, U](value : T) : int { return 2 }\n"}}})"));
  const std::string ambiguous_uri =
      file_uri(workspace.path / "src/ambiguous.janus");
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      ambiguous_uri +
      R"(","text":"def main() : int { return helper() }\n"}}})"));
  const auto ambiguous_completion = server.handle(
      R"({"jsonrpc":"2.0","id":31,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      ambiguous_uri + R"("},"position":{"line":0,"character":28}}})");
  JANUS_REQUIRE(ambiguous_completion.front().find("from a") !=
                std::string::npos);
  JANUS_REQUIRE(ambiguous_completion.front().find("from b") !=
                std::string::npos);
  JANUS_REQUIRE(named_items_have_no_edits(ambiguous_completion.front(),
                                          "helper"));
  JANUS_REQUIRE(occurrences(ambiguous_completion.front(),
                            "\"label\":\"helper\"") >= 2);
  const auto typed_completion = server.handle(
      R"({"jsonrpc":"2.0","id":33,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      ambiguous_uri + R"("},"position":{"line":0,"character":28}}})");
  JANUS_REQUIRE(typed_completion.front().find(
                    "\"detail\":\"class Widget() — from b (workspace)\",\"kind\":7") !=
                std::string::npos);
  JANUS_REQUIRE(typed_completion.front().find(
                    "\"detail\":\"trait Printable — from b (workspace)\",\"kind\":8") !=
                std::string::npos);
  JANUS_REQUIRE(typed_completion.front().find(
                    "\"detail\":\"enum Status — from b (workspace)\",\"kind\":13") !=
                std::string::npos);
  const std::string contextual_uri =
      file_uri(workspace.path / "src/contextual.janus");
  const std::string contextual_source =
      "def main() : int { val result : int = maker; return 0 }\\n";
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      contextual_uri + R"(","text":")" + contextual_source + R"("}}})"));
  const auto expected_type_completion = server.handle(
      R"({"jsonrpc":"2.0","id":34,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      contextual_uri + R"("},"position":{"line":0,"character":43}}})");
  JANUS_REQUIRE(expected_type_completion.front().find("intMaker") <
                expected_type_completion.front().find("stringMaker"));

  const std::string argument_source =
      "def main() : int { return candidate(1, 2) }\\n";
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
      contextual_uri + R"("},"contentChanges":[{"text":")" +
      argument_source + R"("}]}})"));
  const auto argument_count_completion = server.handle(
      R"({"jsonrpc":"2.0","id":35,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      contextual_uri + R"("},"position":{"line":0,"character":35}}})");
  JANUS_REQUIRE(argument_count_completion.front().find("binary") <
                argument_count_completion.front().find("unary"));

  const std::string generic_source =
      "def main() : int { return candidate[int](1) }\\n";
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
      contextual_uri + R"("},"contentChanges":[{"text":")" +
      generic_source + R"("}]}})"));
  const auto generic_count_completion = server.handle(
      R"({"jsonrpc":"2.0","id":43,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      contextual_uri + R"("},"position":{"line":0,"character":35}}})");
  JANUS_REQUIRE(generic_count_completion.front().find("zGeneric") <
                generic_count_completion.front().find("aPair"));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      qualified_consumer_uri +
      R"(","text":"import a\nimport b\n\ndef main() : int { return a.helper() }\n"}}})"));
  const std::vector<std::string> qualified_references = server.handle(
      R"({"jsonrpc":"2.0","id":16,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
      qualified_consumer_uri +
      R"("},"position":{"line":3,"character":29},"context":{"includeDeclaration":true}}})");
  JANUS_REQUIRE(qualified_references.front().find(module_a_uri) !=
                std::string::npos);
  JANUS_REQUIRE(qualified_references.front().find(qualified_consumer_uri) !=
                std::string::npos);
  JANUS_REQUIRE(qualified_references.front().find(module_b_uri) ==
                std::string::npos);
  const std::vector<std::string> qualified_rename = server.handle(
      R"({"jsonrpc":"2.0","id":17,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
      qualified_consumer_uri +
      R"("},"position":{"line":3,"character":29},"newName":"renamedQualified"}})");
  JANUS_REQUIRE(occurrences(qualified_rename.front(),
                            "\"newText\":\"renamedQualified\"") == 2);
  JANUS_REQUIRE(qualified_rename.front().find(module_a_uri) !=
                std::string::npos);
  JANUS_REQUIRE(qualified_rename.front().find(qualified_consumer_uri) !=
                std::string::npos);
  JANUS_REQUIRE(qualified_rename.front().find(module_b_uri) ==
                std::string::npos);

  const std::string alias_consumer_uri =
      file_uri(workspace.path / "src/alias-consumer.janus");
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      alias_consumer_uri +
      R"(","text":"import a as alpha\n\ndef main() : int { return alpha.helper() }\n"}}})"));
  const auto alias_top_level_completion = server.handle(
      R"({"jsonrpc":"2.0","id":36,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      alias_consumer_uri + R"("},"position":{"line":2,"character":25}}})");
  JANUS_REQUIRE(alias_top_level_completion.front().find(
                    "\"label\":\"uniqueA\"") == std::string::npos);
  const std::vector<std::string> alias_definition = server.handle(
      R"({"jsonrpc":"2.0","id":18,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      alias_consumer_uri + R"("},"position":{"line":2,"character":34}}})");
  JANUS_REQUIRE(alias_definition.front().find(module_a_uri) !=
                std::string::npos);
  const std::vector<std::string> alias_hover = server.handle(
      R"({"jsonrpc":"2.0","id":19,"method":"textDocument/hover","params":{"textDocument":{"uri":")" +
      alias_consumer_uri + R"("},"position":{"line":2,"character":34}}})");
  JANUS_REQUIRE(alias_hover.front().find("alpha.helper (alias of a.helper)") !=
                std::string::npos);
  const std::vector<std::string> alias_completion = server.handle(
      R"({"jsonrpc":"2.0","id":20,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      alias_consumer_uri + R"("},"position":{"line":2,"character":32}}})");
  JANUS_REQUIRE(alias_completion.front().find("\"label\":\"helper\"") !=
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
  JANUS_REQUIRE(selective_definition.front().find(module_a_uri) !=
                std::string::npos);
  const std::vector<std::string> local_alias_rename = server.handle(
      R"({"jsonrpc":"2.0","id":22,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
      selective_consumer_uri +
      R"("},"position":{"line":2,"character":28},"newName":"localAnswer"}})");
  JANUS_REQUIRE(occurrences(local_alias_rename.front(),
                            "\"newText\":\"localAnswer\"") == 2);
  JANUS_REQUIRE(local_alias_rename.front().find(selective_consumer_uri) !=
                std::string::npos);
  JANUS_REQUIRE(local_alias_rename.front().find(module_a_uri) ==
                std::string::npos);

  const std::string selective_edit_uri =
      file_uri(workspace.path / "src/selective-edit.janus");
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      selective_edit_uri +
      R"(","text":"import a.{helper}\n\ndef main() : int { return uniqueA() }\n"}}})"));
  const auto selective_edit_completion = server.handle(
      R"({"jsonrpc":"2.0","id":32,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      selective_edit_uri + R"("},"position":{"line":2,"character":28}}})");
  JANUS_REQUIRE(selective_edit_completion.front().find(
                    "import a.{helper, uniqueA}") != std::string::npos);
  JANUS_REQUIRE(selective_edit_completion.front().find(
                    "\"newText\":\"import a\\n\"") ==
                std::string::npos);

  TemporaryWorkspace indexed_workspace{
      std::filesystem::temp_directory_path() /
      ("janus-lsp-external-index-" + std::to_string(suffix))};
  std::filesystem::create_directories(indexed_workspace.path / "src");
  const std::filesystem::path external_index_path =
      indexed_workspace.path / "stdlib-api-index.json";
  janus::driver::ApiIndex external_index;
  external_index.package = "offline-stdlib";
  external_index.symbols.push_back(janus::driver::ApiSymbol{
      "offlineClock", "std.time.offlineClock", "offline-stdlib", "std.time",
      "std.time", "function", "def offlineClock() : int", {}, {}, {}, "int",
      {}, {}, "public", {}, false, std::nullopt});
  external_index.symbols.push_back(janus::driver::ApiSymbol{
      "tick", "std.time.Clock.tick", "offline-stdlib", "std.time",
      "std.time", "method", "def tick() : int", {}, {}, {}, "int", {}, {},
      "public", {}, false, std::nullopt});
  const std::filesystem::path peer_index_path =
      indexed_workspace.path / "peer-api-index.json";
  janus::driver::ApiIndex peer_index;
  peer_index.package = "peer-package";
  peer_index.symbols.push_back(janus::driver::ApiSymbol{
      "sharedNeedle", "std.time.sharedNeedle", "peer-package", "std.time",
      "std.time", "function", "def sharedNeedle() : int", {}, {}, {}, "int",
      {}, {}, "public", {}, false, std::nullopt});
  external_index.symbols.push_back(janus::driver::ApiSymbol{
      "sharedNeedle", "std.time.sharedNeedle", "offline-stdlib", "std.time",
      "std.time", "function", "def sharedNeedle() : int", {}, {}, {}, "int",
      {}, {}, "public", {}, false, std::nullopt});
  janus::driver::write_api_index(external_index, external_index_path);
  janus::driver::write_api_index(peer_index, peer_index_path);
  const std::filesystem::path indexed_main_path =
      indexed_workspace.path / "src/main.janus";
  {
    std::ofstream output{indexed_main_path};
    output << "def main() : int { return offlineClock() }\n";
  }
  janus::lsp::Server indexed_server{{},
                                    {external_index_path, peer_index_path}};
  const std::string indexed_root_uri = file_uri(indexed_workspace.path);
  const std::string indexed_main_uri = file_uri(indexed_main_path);
  static_cast<void>(indexed_server.handle(
      R"({"jsonrpc":"2.0","id":37,"method":"initialize","params":{"rootUri":")" +
      indexed_root_uri + R"("}})"));
  static_cast<void>(indexed_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      indexed_main_uri +
      R"(","text":"def main() : int { return offlineClock() }\n"}}})"));
  const auto external_completion = indexed_server.handle(
      R"({"jsonrpc":"2.0","id":38,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      indexed_main_uri + R"("},"position":{"line":0,"character":35}}})");
  JANUS_REQUIRE(external_completion.front().find(
                    "\"label\":\"offlineClock\"") != std::string::npos);
  JANUS_REQUIRE(external_completion.front().find(
                    "import std.time\\n") != std::string::npos);
  JANUS_REQUIRE(external_completion.front().find("\"label\":\"tick\"") ==
                std::string::npos);
  const auto external_action = indexed_server.handle(
      R"({"jsonrpc":"2.0","id":41,"method":"textDocument/codeAction","params":{"textDocument":{"uri":")" +
      indexed_main_uri +
      R"("},"range":{"start":{"line":0,"character":26},"end":{"line":0,"character":38}},"context":{"diagnostics":[]}}})");
  JANUS_REQUIRE(external_action.front().find(
                    "Import module `std.time`") != std::string::npos);

  static_cast<void>(indexed_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
      indexed_main_uri +
      R"("},"contentChanges":[{"text":"def main() : int { return sharedNeedle() }\n"}]}})"));
  const auto collision_completion = indexed_server.handle(
      R"({"jsonrpc":"2.0","id":44,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      indexed_main_uri + R"("},"position":{"line":0,"character":35}}})");
  JANUS_REQUIRE(occurrences(collision_completion.front(),
                            "\"label\":\"sharedNeedle\"") == 2);
  JANUS_REQUIRE(named_items_have_no_edits(collision_completion.front(),
                                          "sharedNeedle"));
  const auto collision_action = indexed_server.handle(
      R"({"jsonrpc":"2.0","id":45,"method":"textDocument/codeAction","params":{"textDocument":{"uri":")" +
      indexed_main_uri +
      R"("},"range":{"start":{"line":0,"character":26},"end":{"line":0,"character":38}},"context":{"diagnostics":[]}}})");
  JANUS_REQUIRE(collision_action.front().find("Import module `std.time`") ==
                std::string::npos);

  static_cast<void>(indexed_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
      indexed_main_uri +
      R"("},"contentChanges":[{"text":"import std.time as time\n\ndef main() : int { return time.offlineClock() }\n"}]}})"));
  const auto external_member_completion = indexed_server.handle(
      R"({"jsonrpc":"2.0","id":42,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      indexed_main_uri + R"("},"position":{"line":2,"character":31}}})");
  JANUS_REQUIRE(external_member_completion.front().find(
                    "\"label\":\"offlineClock\"") != std::string::npos);
  JANUS_REQUIRE(named_items_have_no_edits(external_member_completion.front(),
                                          "offlineClock"));

  TemporaryWorkspace indexed_dependency_workspace{
      std::filesystem::temp_directory_path() /
      ("janus-lsp-indexed-dependency-" + std::to_string(suffix))};
  std::filesystem::create_directories(indexed_dependency_workspace.path /
                                      "src");
  std::filesystem::create_directories(indexed_dependency_workspace.path /
                                      "deps/offline/target/doc");
  {
    std::ofstream output{indexed_dependency_workspace.path / "janus.toml"};
    output << "[package]\nname = \"consumer\"\nversion = \"0.1.0\"\n"
              "entry = \"src/main.janus\"\n\n[dependencies]\n"
              "offline = { path = \"deps/offline\" }\n";
  }
  {
    std::ofstream output{indexed_dependency_workspace.path /
                         "deps/offline/janus.toml"};
    output << "[package]\nname = \"offline\"\nversion = \"0.1.0\"\n"
              "entry = \"src/missing.janus\"\n";
  }
  const std::filesystem::path indexed_dependency_main =
      indexed_dependency_workspace.path / "src/main.janus";
  {
    std::ofstream output{indexed_dependency_main};
    output << "def main() : int { return cachedDependency() }\n";
  }
  janus::driver::ApiIndex dependency_index;
  dependency_index.package = "offline";
  dependency_index.symbols.push_back(janus::driver::ApiSymbol{
      "cachedDependency", "offline.api.cachedDependency", "offline",
      "offline.api", "offline.api", "function",
      "def cachedDependency() : int", {}, {}, {}, "int", {}, {}, "public",
      {}, false, std::nullopt});
  janus::driver::write_api_index(
      dependency_index, indexed_dependency_workspace.path /
                            "deps/offline/target/doc/api-index.json");
  janus::lsp::Server dependency_index_server;
  const std::string dependency_index_root_uri =
      file_uri(indexed_dependency_workspace.path);
  const std::string dependency_index_main_uri = file_uri(indexed_dependency_main);
  static_cast<void>(dependency_index_server.handle(
      R"({"jsonrpc":"2.0","id":39,"method":"initialize","params":{"rootUri":")" +
      dependency_index_root_uri + R"("}})"));
  static_cast<void>(dependency_index_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      dependency_index_main_uri +
      R"(","text":"def main() : int { return cachedDependency() }\n"}}})"));
  const auto dependency_index_completion = dependency_index_server.handle(
      R"({"jsonrpc":"2.0","id":40,"method":"textDocument/completion","params":{"textDocument":{"uri":")" +
      dependency_index_main_uri + R"("},"position":{"line":0,"character":42}}})");
  JANUS_REQUIRE(dependency_index_completion.front().find(
                    "\"label\":\"cachedDependency\"") != std::string::npos);
  JANUS_REQUIRE(dependency_index_completion.front().find(
                    "import offline.api\\n") != std::string::npos);

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
  JANUS_REQUIRE(private_rename.front().find(library_uri) != std::string::npos);
  JANUS_REQUIRE(private_rename.front().find(main_uri) == std::string::npos);

  const std::vector<std::string> workspace_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":5,"method":"workspace/symbol","params":{"query":""}})");
  JANUS_REQUIRE(workspace_symbols.front().find("workspaceValue") !=
                std::string::npos);
  JANUS_REQUIRE(workspace_symbols.front().find("helper") != std::string::npos);
  JANUS_REQUIRE(workspace_symbols.front().find("hiddenWorkspaceValue") ==
                std::string::npos);
  JANUS_REQUIRE(workspace_symbols.front().find("secretHelper") ==
                std::string::npos);

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
  JANUS_REQUIRE(saved_symbols.front().find("savedValue") != std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":")" +
      unopened_uri + R"("}}})"));
  const std::vector<std::string> reloaded_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":13,"method":"workspace/symbol","params":{"query":"workspaceValue"}})");
  JANUS_REQUIRE(reloaded_symbols.front().find("workspaceValue") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"contentChanges":[{"text":"import unopened\n\ndef main() : int { return workspaceValue }"}]}})"));
  const std::vector<std::string> unopened_definition = server.handle(
      R"({"jsonrpc":"2.0","id":6,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      main_uri + R"("},"position":{"line":2,"character":30}}})");
  JANUS_REQUIRE(unopened_definition.front().find("src/unopened.janus") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
      main_uri +
      R"("},"contentChanges":[{"text":"def main() : int { return secretHelper() }"}]}})"));
  const std::vector<std::string> private_definition = server.handle(
      R"({"jsonrpc":"2.0","id":7,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      main_uri + R"("},"position":{"line":0,"character":30}}})");
  JANUS_REQUIRE(private_definition.front().find("\"result\":null") !=
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
  JANUS_REQUIRE(created_symbols.front().find("createdValue") !=
                std::string::npos);
  JANUS_REQUIRE(server.workspace_index_metrics().files == 5);

  {
    std::ofstream output{created_path};
    output << "module created\n\nval updatedValue : int = 2\n";
  }
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":")" +
      created_uri + R"(","type":2}]}})"));
  const std::vector<std::string> updated_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":9,"method":"workspace/symbol","params":{"query":"updatedValue"}})");
  JANUS_REQUIRE(updated_symbols.front().find("updatedValue") !=
                std::string::npos);
  JANUS_REQUIRE(updated_symbols.front().find("createdValue") ==
                std::string::npos);

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
  JANUS_REQUIRE(warning_opened.front().find("\"code\":\"JANA0014\"") !=
                std::string::npos);
  const std::vector<std::string> warning_closed = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":")" +
      created_uri + R"("}}})");
  JANUS_REQUIRE(warning_closed.front().find("\"code\":\"JANA0014\"") !=
                std::string::npos);

  std::filesystem::remove(created_path);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":")" +
      created_uri + R"(","type":3}]}})"));
  const std::vector<std::string> deleted_symbols = server.handle(
      R"({"jsonrpc":"2.0","id":10,"method":"workspace/symbol","params":{"query":"updatedValue"}})");
  JANUS_REQUIRE(deleted_symbols.front().find("updatedValue") ==
                std::string::npos);
  JANUS_REQUIRE(server.workspace_index_metrics().files == 4);

  const std::vector<std::string> stats = server.handle(
      R"({"jsonrpc":"2.0","id":11,"method":"janus/workspaceIndexStats","params":{}})");
  JANUS_REQUIRE(stats.front().find("\"files\":4") != std::string::npos);
  JANUS_REQUIRE(stats.front().find("\"estimatedMemoryBytes\"") !=
                std::string::npos);

  TemporaryWorkspace competing_workspace{
      std::filesystem::temp_directory_path() /
      ("janus-lsp-competing-modules-" + std::to_string(suffix))};
  std::filesystem::create_directories(competing_workspace.path / "src");
  std::filesystem::create_directories(competing_workspace.path /
                                      "deps/library/src");
  {
    std::ofstream output{competing_workspace.path / "janus.toml"};
    output << "[package]\nname = \"competing\"\nversion = \"0.1.0\"\n"
              "entry = \"src/main.janus\"\n"
              "\n[dependencies]\nlibrary = { path = \"deps/library\" }\n";
  }
  {
    std::ofstream output{competing_workspace.path / "deps/library/janus.toml"};
    output << "[package]\nname = \"library\"\nversion = \"0.1.0\"\n"
              "entry = \"src/shared.janus\"\n";
  }
  const std::filesystem::path competing_main_path =
      competing_workspace.path / "src/main.janus";
  const std::filesystem::path project_shared_path =
      competing_workspace.path / "src/shared.janus";
  const std::filesystem::path dependency_shared_path =
      competing_workspace.path / "deps/library/src/shared.janus";
  const std::filesystem::path dependency_consumer_path =
      competing_workspace.path / "deps/library/src/consumer.janus";
  {
    std::ofstream output{competing_main_path};
    output << "import consumer\nimport shared\n\ndef main() : int { return chosen() }\n";
  }
  {
    std::ofstream output{project_shared_path};
    output << "module shared\n\ndef chosen() : int { return 1 }\n";
  }
  {
    std::ofstream output{dependency_shared_path};
    output << "module shared\n\ndef chosen(value : int) : int { return value }\n";
  }
  {
    std::ofstream output{dependency_consumer_path};
    output << "module consumer\nimport shared\n\ndef use() : int { return chosen() }\n";
  }

  janus::frontend::ModuleLoader compiler_loader{
      {competing_workspace.path / "deps/library/src"}};
  const janus::ast::Program compiled = compiler_loader.load(competing_main_path);
  JANUS_REQUIRE(std::any_of(compiled.functions.begin(), compiled.functions.end(),
                            [](const auto &function) {
                              return function.name == "chosen" &&
                                     function.parameters.empty();
                            }));

  janus::lsp::Server competing_server;
  const std::string competing_root_uri = file_uri(competing_workspace.path);
  const std::string competing_main_uri = file_uri(competing_main_path);
  const std::string project_shared_uri = file_uri(project_shared_path);
  const std::string dependency_shared_uri = file_uri(dependency_shared_path);
  const std::string dependency_consumer_uri =
      file_uri(dependency_consumer_path);
  static_cast<void>(competing_server.handle(
      R"({"jsonrpc":"2.0","id":30,"method":"initialize","params":{"rootUri":")" +
      competing_root_uri + R"("}})"));
  static_cast<void>(competing_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      competing_main_uri +
      R"(","text":"import consumer\nimport shared\n\ndef main() : int { return chosen() }\n"}}})"));
  const std::vector<std::string> competing_definition = competing_server.handle(
      R"({"jsonrpc":"2.0","id":31,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      competing_main_uri +
      R"("},"position":{"line":3,"character":30}}})");
  JANUS_REQUIRE(competing_definition.front().find(project_shared_uri) !=
                std::string::npos);
  JANUS_REQUIRE(competing_definition.front().find(dependency_shared_uri) ==
                std::string::npos);
  const std::vector<std::string> competing_hover = competing_server.handle(
      R"({"jsonrpc":"2.0","id":32,"method":"textDocument/hover","params":{"textDocument":{"uri":")" +
      competing_main_uri +
      R"("},"position":{"line":3,"character":30}}})");
  JANUS_REQUIRE(competing_hover.front().find("def chosen() : int") !=
                std::string::npos);
  JANUS_REQUIRE(competing_hover.front().find("value : int") ==
                std::string::npos);
  const std::vector<std::string> competing_references =
      competing_server.handle(
          R"({"jsonrpc":"2.0","id":33,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
          competing_main_uri +
          R"("},"position":{"line":3,"character":30},"context":{"includeDeclaration":true}}})");
  JANUS_REQUIRE(competing_references.front().find(competing_main_uri) !=
                std::string::npos);
  JANUS_REQUIRE(competing_references.front().find(project_shared_uri) !=
                std::string::npos);
  JANUS_REQUIRE(competing_references.front().find(dependency_consumer_uri) !=
                std::string::npos);
  JANUS_REQUIRE(competing_references.front().find(dependency_shared_uri) ==
                std::string::npos);
  const std::vector<std::string> competing_rename = competing_server.handle(
      R"({"jsonrpc":"2.0","id":34,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
      competing_main_uri +
      R"("},"position":{"line":3,"character":30},"newName":"projectChosen"}})");
  JANUS_REQUIRE(competing_rename.front().find(competing_main_uri) !=
                std::string::npos);
  JANUS_REQUIRE(competing_rename.front().find(project_shared_uri) !=
                std::string::npos);
  JANUS_REQUIRE(competing_rename.front().find(dependency_consumer_uri) !=
                std::string::npos);
  JANUS_REQUIRE(competing_rename.front().find(dependency_shared_uri) ==
                std::string::npos);

  static_cast<void>(competing_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      dependency_consumer_uri +
      R"(","text":"module consumer\nimport shared\n\ndef use() : int { return chosen() }\n"}}})"));
  const std::vector<std::string> dependency_definition =
      competing_server.handle(
          R"({"jsonrpc":"2.0","id":35,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
          dependency_consumer_uri +
          R"("},"position":{"line":3,"character":29}}})");
  JANUS_REQUIRE(dependency_definition.front().find(dependency_shared_uri) !=
                std::string::npos);
  JANUS_REQUIRE(dependency_definition.front().find(project_shared_uri) ==
                std::string::npos);

  janus::lsp::Server reverse_index_server;
  static_cast<void>(reverse_index_server.handle(
      R"({"jsonrpc":"2.0","id":36,"method":"initialize","params":{"rootUri":")" +
      competing_root_uri + R"("}})"));
  static_cast<void>(reverse_index_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      dependency_shared_uri +
      R"(","text":"module shared\n\ndef chosen(value : int) : int { return value }\n"}}})"));
  static_cast<void>(reverse_index_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      project_shared_uri +
      R"(","text":"module shared\n\ndef chosen() : int { return 1 }\n"}}})"));
  static_cast<void>(reverse_index_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      competing_main_uri +
      R"(","text":"import shared\n\ndef main() : int { return chosen() }\n"}}})"));
  const std::vector<std::string> reverse_index_definition =
      reverse_index_server.handle(
          R"({"jsonrpc":"2.0","id":37,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
          competing_main_uri +
          R"("},"position":{"line":2,"character":30}}})");
  JANUS_REQUIRE(reverse_index_definition.front().find(project_shared_uri) !=
                std::string::npos);
  JANUS_REQUIRE(reverse_index_definition.front().find(dependency_shared_uri) ==
                std::string::npos);

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
  JANUS_REQUIRE(encoded_symbols.front().find(encoded_file_uri) !=
                std::string::npos);
  JANUS_REQUIRE(encoded_symbols.front().find("module # %.janus") ==
                std::string::npos);

#ifndef _WIN32
  TemporaryWorkspace symlink_workspace{
      std::filesystem::temp_directory_path() /
      ("janus-lsp-symlink-buffer-" + std::to_string(suffix))};
  std::filesystem::create_directories(symlink_workspace.path / "src");
  const std::filesystem::path symlink_main =
      symlink_workspace.path / "src/main.janus";
  const std::filesystem::path symlink_target =
      symlink_workspace.path / "src/real.janus";
  const std::filesystem::path symlink_module =
      symlink_workspace.path / "src/shared.janus";
  {
    std::ofstream output{symlink_workspace.path / "janus.toml"};
    output << "[package]\nname = \"symlink-buffer\"\nversion = \"0.1.0\"\n"
              "entry = \"src/main.janus\"\n";
  }
  {
    std::ofstream output{symlink_main};
    output << "import shared\n\ndef main() : int { return buffered() }\n";
  }
  {
    std::ofstream output{symlink_target};
    output << "module shared\n\ndef stale() : int { return 0 }\n";
  }
  std::error_code symlink_error;
  std::filesystem::create_symlink(symlink_target.filename(), symlink_module,
                                  symlink_error);
  JANUS_REQUIRE(!symlink_error);

  janus::lsp::Server symlink_server;
  static_cast<void>(symlink_server.handle(
      R"({"jsonrpc":"2.0","id":50,"method":"initialize","params":{"rootUri":")" +
      file_uri(symlink_workspace.path) + R"("}})"));
  static_cast<void>(symlink_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      file_uri(symlink_module) +
      R"(","text":"module shared\n\ndef buffered() : int { return 1 }\n"}}})"));
  static_cast<void>(symlink_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
      file_uri(symlink_main) +
      R"(","text":"import shared\n\ndef main() : int { return buffered() }\n"}}})"));
  const std::vector<std::string> symlink_definition = symlink_server.handle(
      R"({"jsonrpc":"2.0","id":51,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
      file_uri(symlink_main) +
      R"("},"position":{"line":2,"character":30}}})");
  JANUS_REQUIRE(symlink_definition.front().find(file_uri(symlink_target)) !=
                std::string::npos);
  const std::vector<std::string> symlink_hover = symlink_server.handle(
      R"({"jsonrpc":"2.0","id":52,"method":"textDocument/hover","params":{"textDocument":{"uri":")" +
      file_uri(symlink_main) +
      R"("},"position":{"line":2,"character":30}}})");
  JANUS_REQUIRE(symlink_hover.front().find("def buffered() : int") !=
                std::string::npos);
  JANUS_REQUIRE(symlink_hover.front().find("stale") == std::string::npos);
#endif
  return 0;
}
