#include "janus/lsp/server.hpp"

#include "../support/require.hpp"

#include <charconv>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <llvm/Support/JSON.h>

#include <iostream>

namespace {

std::vector<std::int64_t> semantic_token_field(const std::string &response,
                                               std::size_t field) {
  JANUS_REQUIRE(field < 5);
  const std::string marker = "\"data\":[";
  std::size_t cursor = response.find(marker);
  JANUS_REQUIRE(cursor != std::string::npos);
  cursor += marker.size();
  std::vector<std::int64_t> values;
  while (cursor < response.size() && response[cursor] != ']') {
    std::int64_t value = 0;
    const char *begin = response.data() + cursor;
    const char *end = response.data() + response.size();
    const auto parsed = std::from_chars(begin, end, value);
    JANUS_REQUIRE(parsed.ec == std::errc{});
    values.push_back(value);
    cursor = static_cast<std::size_t>(parsed.ptr - response.data());
    if (cursor < response.size() && response[cursor] == ',')
      ++cursor;
  }
  JANUS_REQUIRE(values.size() % 5 == 0);
  std::vector<std::int64_t> fields;
  for (std::size_t index = field; index < values.size(); index += 5)
    fields.push_back(values[index]);
  return fields;
}

std::int64_t semantic_token_type_at(const std::string &response,
                                    std::int64_t expected_line,
                                    std::int64_t expected_column) {
  const std::vector<std::int64_t> line_deltas =
      semantic_token_field(response, 0);
  const std::vector<std::int64_t> column_deltas =
      semantic_token_field(response, 1);
  const std::vector<std::int64_t> types = semantic_token_field(response, 3);
  std::int64_t line = 0;
  std::int64_t column = 0;
  for (std::size_t index = 0; index < types.size(); ++index) {
    line += line_deltas[index];
    column = line_deltas[index] == 0 ? column + column_deltas[index]
                                     : column_deltas[index];
    if (line == expected_line && column == expected_column)
      return types[index];
  }
  JANUS_REQUIRE(false);
  return -1;
}

enum class LspResultShape { CodeActions, FormattingEdits, SemanticTokens,
                            InlayHints };

std::string require_lsp_result(const std::vector<std::string> &responses,
                               LspResultShape shape) {
  JANUS_REQUIRE(responses.size() == 1);
  llvm::Expected<llvm::json::Value> parsed =
      llvm::json::parse(responses.front());
  JANUS_REQUIRE(static_cast<bool>(parsed));
  const llvm::json::Object *message = parsed->getAsObject();
  JANUS_REQUIRE(message != nullptr);
  JANUS_REQUIRE(message->getString("jsonrpc") == "2.0");
  JANUS_REQUIRE(message->get("error") == nullptr);
  JANUS_REQUIRE(responses.front().find("-32601") == std::string::npos);
  const llvm::json::Value *result = message->get("result");
  JANUS_REQUIRE(result != nullptr);
  if (shape == LspResultShape::SemanticTokens) {
    const llvm::json::Object *tokens = result->getAsObject();
    JANUS_REQUIRE(tokens != nullptr);
    JANUS_REQUIRE(tokens->getArray("data") != nullptr);
    return responses.front();
  }
  const llvm::json::Array *items = result->getAsArray();
  JANUS_REQUIRE(items != nullptr);
  for (const llvm::json::Value &item : *items) {
    const llvm::json::Object *object = item.getAsObject();
    JANUS_REQUIRE(object != nullptr);
    if (shape == LspResultShape::CodeActions) {
      JANUS_REQUIRE(object->getString("title").has_value());
      JANUS_REQUIRE(object->getString("kind").has_value());
    } else if (shape == LspResultShape::FormattingEdits) {
      JANUS_REQUIRE(object->getObject("range") != nullptr);
      JANUS_REQUIRE(object->getString("newText").has_value());
    } else {
      JANUS_REQUIRE(object->getObject("position") != nullptr);
      JANUS_REQUIRE(object->get("label") != nullptr);
    }
  }
  return responses.front();
}

void require_hover_result(const std::vector<std::string> &responses,
                          std::string_view expected_detail,
                          std::int64_t expected_line,
                          std::int64_t expected_start,
                          std::int64_t expected_end) {
  JANUS_REQUIRE(responses.size() == 1);
  llvm::Expected<llvm::json::Value> parsed =
      llvm::json::parse(responses.front());
  JANUS_REQUIRE(static_cast<bool>(parsed));
  const llvm::json::Object *message = parsed->getAsObject();
  JANUS_REQUIRE(message != nullptr);
  JANUS_REQUIRE(message->get("error") == nullptr);
  const llvm::json::Object *result = message->getObject("result");
  JANUS_REQUIRE(result != nullptr);
  const llvm::json::Object *contents = result->getObject("contents");
  JANUS_REQUIRE(contents != nullptr);
  const std::optional<llvm::StringRef> value = contents->getString("value");
  JANUS_REQUIRE(value.has_value());
  JANUS_REQUIRE(value->contains(expected_detail));
  const llvm::json::Object *hover_range = result->getObject("range");
  JANUS_REQUIRE(hover_range != nullptr);
  const llvm::json::Object *start = hover_range->getObject("start");
  const llvm::json::Object *end = hover_range->getObject("end");
  JANUS_REQUIRE(start != nullptr && end != nullptr);
  JANUS_REQUIRE(start->getInteger("line") == expected_line);
  JANUS_REQUIRE(end->getInteger("line") == expected_line);
  JANUS_REQUIRE(start->getInteger("character") == expected_start);
  JANUS_REQUIRE(end->getInteger("character") == expected_end);
}

void require_null_result(const std::vector<std::string> &responses) {
  JANUS_REQUIRE(responses.size() == 1);
  JANUS_REQUIRE(responses.front().find("\"result\":null") !=
                std::string::npos);
  JANUS_REQUIRE(responses.front().find("\"error\"") == std::string::npos);
}

void require_safe_correction(const std::string &response) {
  llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(response);
  JANUS_REQUIRE(static_cast<bool>(parsed));
  const llvm::json::Object *message = parsed->getAsObject();
  JANUS_REQUIRE(message != nullptr);
  const llvm::json::Array *actions = message->getArray("result");
  JANUS_REQUIRE(actions != nullptr && actions->size() == 1);
  const llvm::json::Object *action = actions->front().getAsObject();
  JANUS_REQUIRE(action != nullptr);
  JANUS_REQUIRE(action->getString("title") ==
                "remove the unexpected character");
  JANUS_REQUIRE(action->getString("kind") == "quickfix");
  JANUS_REQUIRE(action->getBoolean("isPreferred") == true);
  const llvm::json::Object *edit = action->getObject("edit");
  JANUS_REQUIRE(edit != nullptr);
  const llvm::json::Object *changes = edit->getObject("changes");
  JANUS_REQUIRE(changes != nullptr);
  JANUS_REQUIRE(changes->size() == 1);
  const llvm::json::Array *edits = changes->begin()->second.getAsArray();
  JANUS_REQUIRE(edits != nullptr && edits->size() == 1);
  const llvm::json::Object *text_edit = edits->front().getAsObject();
  JANUS_REQUIRE(text_edit != nullptr);
  JANUS_REQUIRE(text_edit->getString("newText") == "");
  JANUS_REQUIRE(text_edit->getObject("range") != nullptr);
}

std::string file_uri(const std::filesystem::path &path) {
  std::string normalized =
      std::filesystem::absolute(path).lexically_normal().generic_string();
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

class TemporaryWorkspace final {
public:
  TemporaryWorkspace() {
    static std::atomic<std::uint64_t> counter{};
    const auto stamp = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    bool created = false;
    for (std::uint64_t attempt = 0; attempt < 100 && !created; ++attempt) {
      root_ = std::filesystem::temp_directory_path() /
              ("janus-lsp-" + std::to_string(stamp) + "-" +
               std::to_string(counter.fetch_add(1)) + "-" +
               std::to_string(attempt));
      std::error_code error;
      created = std::filesystem::create_directory(root_, error) && !error;
    }
    JANUS_REQUIRE(created);
    workspace_ = root_ / "workspace with # encoded path";
    std::filesystem::create_directories(workspace_ / "src");
    write(workspace_ / "janus.toml",
          "[package]\nname = \"lsp-source-matrix\"\nversion = \"0.1.0\"\n"
          "entry = \"src/main.janus\"\n");
    write(workspace_ / "src/main.janus",
          "def main() : int { val diskValue = 2 return @0 }\n");
    write(workspace_ / "src/indexed_method_b.janus",
          "module indexed_method_b\n\n"
          "class Box() { def combine(left : int, right : int) : int { return "
          "left } def combine(left : int, middle : int, right : int) : int { "
          "return middle } }\n");
    write(workspace_ / "src/indexed-method-consumer.janus",
          "import indexed_method_b as mb\n\n"
          "def main() : int { val box = new mb.Box() return "
          "box.combine(1, 2) }\n");
#ifndef _WIN32
    write(root_ / "symlink-target.janus",
          "def leaked() : int { val symlinkSecret = 9 return @0 }\n");
    std::error_code symlink_error;
    std::filesystem::create_symlink(root_ / "symlink-target.janus",
                                    workspace_ / "src/cached-link.janus",
                                    symlink_error);
    JANUS_REQUIRE(!symlink_error);
#endif
  }

  TemporaryWorkspace(const TemporaryWorkspace &) = delete;
  TemporaryWorkspace &operator=(const TemporaryWorkspace &) = delete;

  ~TemporaryWorkspace() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  const std::filesystem::path &path() const { return workspace_; }
  std::filesystem::path outside(std::string_view name) const {
    return root_ / std::string{name};
  }

  static void write(const std::filesystem::path &path,
                    std::string_view contents) {
    std::ofstream output{path, std::ios::binary};
    JANUS_REQUIRE(static_cast<bool>(output));
    output << contents;
    JANUS_REQUIRE(static_cast<bool>(output));
  }

private:
  std::filesystem::path root_;
  std::filesystem::path workspace_;
};

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--verify-require-failure")
    JANUS_REQUIRE(false);

  janus::lsp::Server server{
      {std::filesystem::path{JANUS_STDLIB_DIR}},
      {std::filesystem::path{JANUS_STDLIB_API_INDEX}}};

  const std::vector<std::string> initialized = server.handle(
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
  JANUS_REQUIRE(initialized.size() == 1);
  JANUS_REQUIRE(initialized.front().find("\"textDocumentSync\"") !=
                std::string::npos);
  JANUS_REQUIRE(initialized.front().find("\"renameProvider\"") !=
                std::string::npos);
  JANUS_REQUIRE(initialized.front().find("\"prepareProvider\":true") !=
                std::string::npos);
  JANUS_REQUIRE(initialized.front().find("\"signatureHelpProvider\"") !=
                std::string::npos);
  JANUS_REQUIRE(initialized.front().find("\"semanticTokensProvider\"") !=
                std::string::npos);
  JANUS_REQUIRE(initialized.front().find("\"inlayHintProvider\":true") !=
                std::string::npos);
  JANUS_REQUIRE(initialized.front().find("\"implementationProvider\":true") !=
                std::string::npos);
  JANUS_REQUIRE(initialized.front().find("\"codeActionProvider\"") !=
                std::string::npos);

  const std::vector<std::string> invalid = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///broken.janus","version":1,"text":"def main() : int { return nope }"}}})");
  JANUS_REQUIRE(invalid.size() == 1);
  JANUS_REQUIRE(invalid.front().find("publishDiagnostics") !=
                std::string::npos);
  JANUS_REQUIRE(invalid.front().find("unknown value") != std::string::npos);
  JANUS_REQUIRE(invalid.front().find("\"code\":\"JANA0001\"") !=
                std::string::npos);
  JANUS_REQUIRE(invalid.front().find("\"severity\":1") != std::string::npos);
  JANUS_REQUIRE(invalid.front().find("\"version\":1") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":990}})"));
  const std::vector<std::string> cancelled = server.handle(
      R"({"jsonrpc":"2.0","id":990,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":4}}})");
  JANUS_REQUIRE(cancelled.size() == 1);
  JANUS_REQUIRE(cancelled.front().find("\"code\":-32800") !=
                std::string::npos);

  const std::vector<std::string> tailrec_required = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tailrec-required.janus","text":"def loop(value : int) : int { if value == 0 { return 0 } return loop(value - 1) }\ndef main() : int { return loop(2) }"}}})");
  JANUS_REQUIRE(tailrec_required.size() == 1);
  JANUS_REQUIRE(tailrec_required.front().find("\"code\":\"JANA0029\"") !=
                std::string::npos);

  const std::vector<std::string> tailrec_invalid = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tailrec-invalid.janus","text":"tailrec def identity(value : int) : int { return value }\ndef main() : int { return identity(2) }"}}})");
  JANUS_REQUIRE(tailrec_invalid.size() == 1);
  JANUS_REQUIRE(tailrec_invalid.front().find("\"code\":\"JANA0030\"") !=
                std::string::npos);

  const std::vector<std::string> leak_warning = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///leak-warning.janus","text":"class Resource() {}\ndef main() : int {\n    val resource : Resource = new Resource()\n    return 0\n}"}}})");
  JANUS_REQUIRE(leak_warning.size() == 1);
  JANUS_REQUIRE(leak_warning.front().find("\"code\":\"JANA0002\"") !=
                std::string::npos);
  JANUS_REQUIRE(leak_warning.front().find("\"severity\":2") !=
                std::string::npos);
  JANUS_REQUIRE(leak_warning.front().find("may reach the end of its scope") !=
                std::string::npos);

  const std::vector<std::string> ownership_warnings = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///ownership-warnings.janus","text":"class Resource() {}\ndef make() : Resource { return new Resource() }\ndef main() : int {\n    var resource : Resource = new Resource()\n    resource = new Resource()\n    delete resource\n    make()\n    return 0\n}"}}})");
  JANUS_REQUIRE(ownership_warnings.size() == 1);
  JANUS_REQUIRE(ownership_warnings.front().find("\"code\":\"JANA0003\"") !=
                std::string::npos);
  JANUS_REQUIRE(ownership_warnings.front().find("\"code\":\"JANA0004\"") !=
                std::string::npos);

  const std::vector<std::string> analyzer_warnings = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///analyzer-warnings.janus","text":"enum Option[T] { Some(T), None }\ndef maybe() : Option[int] { return Option.None[int]() }\ndef main() : int {\n    val unused : int = 42\n    maybe()\n    return 0\n}"}}})");
  JANUS_REQUIRE(analyzer_warnings.size() == 1);
  JANUS_REQUIRE(analyzer_warnings.front().find("\"code\":\"JANA0005\"") !=
                std::string::npos);
  JANUS_REQUIRE(analyzer_warnings.front().find("\"code\":\"JANA0014\"") !=
                std::string::npos);
  JANUS_REQUIRE(analyzer_warnings.front().find("\"severity\":2") !=
                std::string::npos);

  const std::vector<std::string> raw_ownership_warnings = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///raw-ownership-warnings.janus","text":"class Node(private val next : Node) { destructor { delete next } }\ndef main() : int {\n    val nodes : Ptr[Node] = alloc[Node](usize(1))\n    free(nodes)\n    return 0\n}"}}})");
  JANUS_REQUIRE(raw_ownership_warnings.size() == 1);
  JANUS_REQUIRE(raw_ownership_warnings.front().find("\"code\":\"JANA0015\"") !=
                std::string::npos);
  JANUS_REQUIRE(raw_ownership_warnings.front().find("\"code\":\"JANA0021\"") !=
                std::string::npos);

  const std::vector<std::string> extern_contract = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///extern-contract.janus","text":"extern def inspect(borrow data : Ptr[int]) : Unit\ndef main() : int {\n    val data : Ptr[int] = alloc[int](usize(1))\n    inspect(data)\n    free(data)\n    return 0\n}"}}})");
  JANUS_REQUIRE(extern_contract.size() == 1);
  JANUS_REQUIRE(extern_contract.front().find("\"diagnostics\":[]") !=
                std::string::npos);
  const std::vector<std::string> extern_contract_hover = server.handle(
      R"({"jsonrpc":"2.0","id":54,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///extern-contract.janus"},"position":{"line":3,"character":7}}})");
  JANUS_REQUIRE(extern_contract_hover.size() == 1);
  JANUS_REQUIRE(extern_contract_hover.front().find(
                    "inspect(borrow data : Ptr[int]) : Unit") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///extern-return.janus","text":"extern def data() : borrow Ptr[byte]\ndef main() : int {\n    val result : Ptr[byte] = data()\n    return 0\n}"}}})"));
  const std::vector<std::string> extern_return_hover = server.handle(
      R"({"jsonrpc":"2.0","id":55,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///extern-return.janus"},"position":{"line":2,"character":31}}})");
  JANUS_REQUIRE(extern_return_hover.size() == 1);
  JANUS_REQUIRE(extern_return_hover.front().find("data() : borrow Ptr[byte]") !=
                std::string::npos);

  const std::vector<std::string> safe_correction_diagnostic = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///safe-correction.janus","text":"def main() : int { return @0 }"}}})");
  JANUS_REQUIRE(safe_correction_diagnostic.size() == 1);
  JANUS_REQUIRE(safe_correction_diagnostic.front().find("\"data\"") !=
                std::string::npos);
  JANUS_REQUIRE(safe_correction_diagnostic.front().find(
                    "remove the unexpected character") != std::string::npos);
  const std::vector<std::string> safe_correction = server.handle(
      R"({"jsonrpc":"2.0","id":50,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///safe-correction.janus"},"range":{"start":{"line":0,"character":26},"end":{"line":0,"character":27}},"context":{"diagnostics":[],"only":["quickfix"]}}})");
  JANUS_REQUIRE(safe_correction.size() == 1);
  JANUS_REQUIRE(safe_correction.front().find("\"kind\":\"quickfix\"") !=
                std::string::npos);
  JANUS_REQUIRE(safe_correction.front().find("\"edit\":{\"changes\"") !=
                std::string::npos);
  JANUS_REQUIRE(safe_correction.front().find("\"newText\":\"\"") !=
                std::string::npos);

  const std::vector<std::string> recovered = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///recovery.janus","text":"def first() : int {\n val x : int = }\ndef second() : int {\n val y : int = }\n"}}})");
  JANUS_REQUIRE(recovered.size() == 1);
  std::size_t parser_diagnostic_count = 0;
  std::size_t parser_diagnostic_position = 0;
  while ((parser_diagnostic_position = recovered.front().find(
              "\"code\":\"JPAR0001\"", parser_diagnostic_position)) !=
         std::string::npos) {
    ++parser_diagnostic_count;
    parser_diagnostic_position += 20;
  }
  JANUS_REQUIRE(parser_diagnostic_count == 2);

  const std::vector<std::string> missing_import = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///a/deliberately/long/path/used/to/expose/dangling/diagnostic/messages.janus","text":"import module_that_does_not_exist_anywhere\n\ndef main() : int { return 0 }"}}})");
  JANUS_REQUIRE(missing_import.size() == 1);
  JANUS_REQUIRE(
      missing_import.front().find("cannot resolve imported module "
                                  "'module_that_does_not_exist_anywhere'") !=
      std::string::npos);

  const std::vector<std::string> valid = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { return 0 }"}]}})");
  JANUS_REQUIRE(valid.size() == 1);
  JANUS_REQUIRE(valid.front().find("\"diagnostics\":[]") != std::string::npos);

  const std::vector<std::string> imported = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///array.janus","text":"import std.array\n\ndef main() : int {\n    val values : Array[int] = new Array[int](usize(1))\n    defer delete values\n    return numericCast[int](values.size())\n}\n"}}})");
  JANUS_REQUIRE(imported.size() == 1);
  JANUS_REQUIRE(imported.front().find("\"diagnostics\":[]") !=
                std::string::npos);
  const std::vector<std::string> imported_definition = server.handle(
      R"({"jsonrpc":"2.0","id":15,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///array.janus"},"position":{"line":3,"character":20}}})");
  JANUS_REQUIRE(imported_definition.front().find("stdlib/std/array.janus") !=
                std::string::npos);

  const std::vector<std::string> derivation_document = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///derivation.janus","text":"struct Point(val x : int, val y : int) derives Copy, Equality, Hashing, Debug {}\n\ndef main() : int { return 0 }"}}})");
  JANUS_REQUIRE(derivation_document.size() == 1);
  JANUS_REQUIRE(derivation_document.front().find("\"diagnostics\":[]") !=
                std::string::npos);

  const std::vector<std::string> module = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///library.janus","text":"module library\n\ndef helper() : int { return 42 }"}}})");
  JANUS_REQUIRE(module.size() == 1);
  JANUS_REQUIRE(module.front().find("\"diagnostics\":[]") != std::string::npos);
  JANUS_REQUIRE(module.front().find("entry point") == std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tools.janus","text":"module tools\n\ndef importedAnswer() : int { return 42 }"}}})"));
  const std::vector<std::string> missing_import_diagnostic = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///missing-import.janus","text":"def main() : int { return importedAnswer }"}}})");
  JANUS_REQUIRE(missing_import_diagnostic.front().find("unknown value") !=
                std::string::npos);
  const std::vector<std::string> missing_import_action = server.handle(
      R"({"jsonrpc":"2.0","id":51,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///missing-import.janus"},"range":{"start":{"line":0,"character":26},"end":{"line":0,"character":40}},"context":{"diagnostics":[]}}})");
  JANUS_REQUIRE(missing_import_action.front().find("Import module `tools`") !=
                std::string::npos);
  JANUS_REQUIRE(missing_import_action.front().find(
                    "\"newText\":\"import tools\\n\"") != std::string::npos);
  JANUS_REQUIRE(missing_import_action.front().find("\"isPreferred\":false") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///other-tools.janus","text":"module other_tools\n\ndef importedAnswer() : int { return 7 }"}}})"));
  const std::vector<std::string> ambiguous_import_action = server.handle(
      R"({"jsonrpc":"2.0","id":52,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///missing-import.janus"},"range":{"start":{"line":0,"character":26},"end":{"line":0,"character":40}},"context":{"diagnostics":[]}}})");
  JANUS_REQUIRE(ambiguous_import_action.front().find("\"result\":[]") !=
                std::string::npos);

  const std::vector<std::string> missing_match_diagnostic = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///missing-match.janus","text":"enum Choice { First, Second }\ndef choose(choice : Choice) : int {\n    return match choice { First => 1 }\n}\ndef main() : int { return 0 }"}}})");
  JANUS_REQUIRE(missing_match_diagnostic.front().find("non-exhaustive match") !=
                std::string::npos);
  const std::vector<std::string> missing_match_action = server.handle(
      R"({"jsonrpc":"2.0","id":53,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///missing-match.janus"},"range":{"start":{"line":2,"character":11},"end":{"line":2,"character":16}},"context":{"diagnostics":[]}}})");
  JANUS_REQUIRE(missing_match_action.front().find(
                    "Add missing match branches") != std::string::npos);
  JANUS_REQUIRE(missing_match_action.front().find("\"edit\":{\"changes\"") !=
                std::string::npos);
  JANUS_REQUIRE(missing_match_action.front().find("Second =>") !=
                std::string::npos);
  JANUS_REQUIRE(missing_match_action.front().find("\"isPreferred\":false") !=
                std::string::npos);

  const std::vector<std::string> invalid_module = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///invalid-library.janus","text":"module invalid_library\n\ndef helper() : int { return missing }"}}})");
  JANUS_REQUIRE(invalid_module.size() == 1);
  JANUS_REQUIRE(invalid_module.front().find("unknown value") !=
                std::string::npos);

  const std::vector<std::string> closed = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"file:///array.janus"}}})");
  JANUS_REQUIRE(closed.size() == 1);
  JANUS_REQUIRE(closed.front().find("\"uri\":\"file:///array.janus\"") !=
                std::string::npos);
  JANUS_REQUIRE(closed.front().find("\"diagnostics\":[]") != std::string::npos);
  JANUS_REQUIRE(closed.front().find("entry point") == std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { val answer : int = 42 return answer }"}]}})"));
  const std::vector<std::string> hover = server.handle(
      R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":48}}})");
  require_hover_result(hover, "val answer : int", 0, 48, 54);
  require_null_result(server.handle(
      R"({"jsonrpc":"2.0","id":59,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":54}}})"));

  const std::vector<std::string> definition = server.handle(
      R"({"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":50}}})");
  JANUS_REQUIRE(definition.front().find("\"character\":23") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"val answer : int = 1\ndef main() : int { val answer : int = 2 return answer }"}]}})"));
  const std::vector<std::string> shadowed_definition = server.handle(
      R"({"jsonrpc":"2.0","id":16,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":1,"character":49}}})");
  JANUS_REQUIRE(shadowed_definition.front().find("\"line\":1") !=
                std::string::npos);
  JANUS_REQUIRE(shadowed_definition.front().find("\"character\":23") !=
                std::string::npos);
  const std::vector<std::string> shadowed_references = server.handle(
      R"({"jsonrpc":"2.0","id":17,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":1,"character":49},"context":{"includeDeclaration":true}}})");
  std::size_t reference_count = 0;
  std::size_t reference_position = 0;
  while ((reference_position = shadowed_references.front().find(
              "\"uri\"", reference_position)) != std::string::npos) {
    ++reference_count;
    reference_position += 5;
  }
  JANUS_REQUIRE(reference_count == 2);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { val answer : int = 42 return answer }"}]}})"));

  const std::vector<std::string> references = server.handle(
      R"({"jsonrpc":"2.0","id":4,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":50},"context":{"includeDeclaration":true}}})");
  JANUS_REQUIRE(references.front().find("\"uri\":\"file:///broken.janus\"") !=
                std::string::npos);

  require_null_result(server.handle(
      R"({"jsonrpc":"2.0","id":7,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":0}}})"));
  require_null_result(server.handle(
      R"({"jsonrpc":"2.0","id":8,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":0}}})"));
  require_null_result(server.handle(
      R"({"jsonrpc":"2.0","id":9,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":0},"context":{"includeDeclaration":true}}})"));

  const std::vector<std::string> completion = server.handle(
      R"({"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":20}}})");
  JANUS_REQUIRE(completion.front().find("\"label\":\"answer\"") !=
                std::string::npos);
  JANUS_REQUIRE(completion.front().find("\"label\":\"int\"") !=
                std::string::npos);
  JANUS_REQUIRE(completion.front().find("\"label\":\"return\"") !=
                std::string::npos);
  JANUS_REQUIRE(completion.front().find("\"label\":\"derives\"") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///member-completion.janus","text":"class Basket() { def add(value : int) : Unit {} def size() : usize { return usize(0) } }\nclass Other() { def secret() : Unit {} }\ndef main() : int { val basket : Basket = new Basket() basket. return 0 }"}}})"));
  const std::vector<std::string> typed_member_completion = server.handle(
      R"({"jsonrpc":"2.0","id":50,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///member-completion.janus"},"position":{"line":2,"character":61}}})");
  JANUS_REQUIRE(typed_member_completion.front().find("\"label\":\"add\"") !=
                std::string::npos);
  JANUS_REQUIRE(typed_member_completion.front().find("\"label\":\"size\"") !=
                std::string::npos);
  JANUS_REQUIRE(typed_member_completion.front().find("\"label\":\"secret\"") ==
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///extension-completion.janus","text":"enum Choice { Yes }\nextend Choice { borrow def score(value : int) : int { return value } }\ndef main() : int { val choice : Choice = Choice.Yes() choice. return 0 }"}}})"));
  const std::vector<std::string> extension_member_completion = server.handle(
      R"({"jsonrpc":"2.0","id":501,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///extension-completion.janus"},"position":{"line":2,"character":61}}})");
  JANUS_REQUIRE(extension_member_completion.front().find(
                    "\"label\":\"score\"") != std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///extension-signature.janus","text":"enum Choice { Yes }\nextend Choice { borrow def score(value : int) : int { return value } }\ndef main() : int { val choice : Choice = Choice.Yes() return choice.score(1) }"}}})"));
  const std::vector<std::string> extension_signature = server.handle(
      R"({"jsonrpc":"2.0","id":502,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///extension-signature.janus"},"position":{"line":2,"character":75}}})");
  JANUS_REQUIRE(extension_signature.front().find(
                    "score(value : int) : int [extension]") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///array-member-completion.janus","text":"import std.array\n\ndef main() : int {\n    val xs = [1, 2, 3]\n    xs.\n    defer delete xs\n    return 0\n}\n"}}})"));
  const std::vector<std::string> array_member_completion = server.handle(
      R"({"jsonrpc":"2.0","id":51,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///array-member-completion.janus"},"position":{"line":4,"character":7}}})");
  JANUS_REQUIRE(array_member_completion.front().find("\"label\":\"push\"") !=
                std::string::npos);
  JANUS_REQUIRE(array_member_completion.front().find("\"label\":\"get\"") !=
                std::string::npos);
  JANUS_REQUIRE(array_member_completion.front().find("\"label\":\"size\"") !=
                std::string::npos);
  JANUS_REQUIRE(array_member_completion.front().find("\"label\":\"main\"") ==
                std::string::npos);
  JANUS_REQUIRE(array_member_completion.front().find(
                    "\"label\":\"takeForConsumingIterator\"") ==
          std::string::npos);
  const std::vector<std::string> indexed_open = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///indexed-completion.janus","text":"import std.array\nstruct Item(val value : int) derives Copy {}\ndef main() : int { val items : Array[Item] = new Array[Item](usize(1)) items[usize(0)]. return 0 }"}}})");
  JANUS_REQUIRE(indexed_open.front().find("satisfy Copy") == std::string::npos);
  const std::vector<std::string> indexed_completion = server.handle(
      R"({"jsonrpc":"2.0","id":511,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///indexed-completion.janus"},"position":{"line":2,"character":87}}})");
  JANUS_REQUIRE(indexed_completion.front().find("\"label\":\"value\"") !=
                std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///indexed-token-aware-completion.janus","text":"import std.array\nstruct Item(val value : int) derives Copy {}\ndef pick(value : string) : usize { return usize(0) }\ndef main() : int { val items : Array[Item] = new Array[Item](usize(1)) items[pick(\"]\")]. return 0 }"}}})"));
  const std::vector<std::string> indexed_token_aware_completion = server.handle(
      R"({"jsonrpc":"2.0","id":517,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///indexed-token-aware-completion.janus"},"position":{"line":3,"character":88}}})");
  JANUS_REQUIRE(indexed_token_aware_completion.front().find(
                    "\"label\":\"value\"") != std::string::npos);
  JANUS_REQUIRE(indexed_token_aware_completion.front().find(
                    "\"label\":\"pick\"") == std::string::npos);
  JANUS_REQUIRE(indexed_token_aware_completion.front().find(
                    "\"label\":\"main\"") == std::string::npos);
  JANUS_REQUIRE(indexed_token_aware_completion.front().find(
                    "\"label\":\"items\"") == std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///nested-indexed-completion.janus","text":"import std.array\nclass Item(val value : int) {}\ndef main() : int { val rows : Array[Array[Item]] = new Array[Array[Item]](usize(1)) rows[usize(0)]. return 0 }"}}})"));
  const std::vector<std::string> nested_indexed_completion = server.handle(
      R"({"jsonrpc":"2.0","id":514,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///nested-indexed-completion.janus"},"position":{"line":2,"character":99}}})");
  JANUS_REQUIRE(nested_indexed_completion.front().find(
                    "\"label\":\"push\"") != std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///chained-indexed-completion.janus","text":"import std.array as arrays\nclass Item(val value : int) {}\ndef main() : int { val rows : arrays.Array[arrays.Array[Item]] = new arrays.Array[arrays.Array[Item]](usize(1)) rows[usize(0)][usize(0)]. return 0 }"}}})"));
  const std::vector<std::string> chained_indexed_completion = server.handle(
      R"({"jsonrpc":"2.0","id":515,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///chained-indexed-completion.janus"},"position":{"line":2,"character":137}}})");
  JANUS_REQUIRE(chained_indexed_completion.front().find(
                    "\"label\":\"value\"") != std::string::npos);
  const std::vector<std::string> indexed_diagnostics = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///indexed-diagnostics.janus","text":"import std.array\nclass Resource() {}\ndef main() : int { val values : Array[Resource] = new Array[Resource](usize(1)) val observed : Resource = values[usize(0)] return 0 }"}}})");
  JANUS_REQUIRE(indexed_diagnostics.front().find(
                    "indexed Array read requires element type") !=
                std::string::npos);
  JANUS_REQUIRE(indexed_diagnostics.front().find("satisfy Copy") !=
                std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///indexed-navigation.janus","text":"import std.array\ndef main() : int { val items : Array[int] = [1] return items[usize(0)] }"}}})"));
  const std::vector<std::string> indexed_hover = server.handle(
      R"({"jsonrpc":"2.0","id":512,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///indexed-navigation.janus"},"position":{"line":1,"character":57}}})");
  JANUS_REQUIRE(indexed_hover.front().find("items : Array[int]") !=
                std::string::npos);
  const std::vector<std::string> indexed_definition = server.handle(
      R"({"jsonrpc":"2.0","id":513,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///indexed-navigation.janus"},"position":{"line":1,"character":57}}})");
  JANUS_REQUIRE(indexed_definition.front().find("indexed-navigation.janus") !=
                std::string::npos);
  JANUS_REQUIRE(indexed_definition.front().find("\"line\":1") !=
                std::string::npos);
  JANUS_REQUIRE(indexed_definition.front().find("\"character\":23") !=
                std::string::npos);
  const std::vector<std::string> indexed_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":516,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///indexed-navigation.janus"}}})");
  JANUS_REQUIRE(semantic_token_type_at(indexed_tokens.front(), 1, 55) == 7);
  JANUS_REQUIRE(semantic_token_type_at(indexed_tokens.front(), 1, 67) == 12);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///settings.janus","text":"module settings\n\nval sharedCount : int = 42\nprivate val secretCount : int = 7\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"import settings\n\ndef main() : int { return sharedCount }"}]}})"));

  const std::vector<std::string> global_hover = server.handle(
      R"({"jsonrpc":"2.0","id":10,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":2,"character":26}}})");
  require_hover_result(global_hover, "val sharedCount : int", 2, 26, 37);
  JANUS_REQUIRE(global_hover.front().find("module `settings`") !=
                std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///constant-hover.janus","text":"module constants\nconst answer : int = 6 * 7\ndef main() : int { return answer }"}}})")
                    );
  const std::vector<std::string> constant_hover = server.handle(
      R"({"jsonrpc":"2.0","id":62,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///constant-hover.janus"},"position":{"line":2,"character":27}}})");
  JANUS_REQUIRE(constant_hover.size() == 1);
  JANUS_REQUIRE(constant_hover.front().find("const answer : int = 42") !=
                std::string::npos);
  JANUS_REQUIRE(constant_hover.front().find("origin `constants.answer`") !=
                std::string::npos);
  const std::vector<std::string> constant_hints = server.handle(
      R"({"jsonrpc":"2.0","id":621,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///constant-hover.janus"},"range":{"start":{"line":0,"character":0},"end":{"line":2,"character":34}}}})");
  JANUS_REQUIRE(constant_hints.front().find("\"label\":\" = 42\"") !=
                std::string::npos);
  JANUS_REQUIRE(constant_hints.front().find("origin `constants.answer`") !=
                std::string::npos);
  const auto constant_definition = server.handle(
      R"({"jsonrpc":"2.0","id":622,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///constant-hover.janus"},"position":{"line":2,"character":27}}})");
  JANUS_REQUIRE(constant_definition.front().find("\"line\":1") !=
                std::string::npos);
  const auto constant_references = server.handle(
      R"({"jsonrpc":"2.0","id":623,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///constant-hover.janus"},"position":{"line":2,"character":27},"context":{"includeDeclaration":true}}})");
  JANUS_REQUIRE(constant_references.front().find("constant-hover.janus") !=
                std::string::npos);
  const auto constant_completion = server.handle(
      R"({"jsonrpc":"2.0","id":624,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///constant-hover.janus"},"position":{"line":2,"character":25}}})");
  JANUS_REQUIRE(constant_completion.front().find("const answer : int = 42") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///aliased-hover.janus","text":"import settings.{sharedCount as count}\n\ndef main() : int { return count }"}}})"));
  const std::vector<std::string> aliased_hover = server.handle(
      R"({"jsonrpc":"2.0","id":57,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///aliased-hover.janus"},"position":{"line":2,"character":26}}})");
  require_hover_result(aliased_hover, "val sharedCount : int", 2, 26, 31);
  JANUS_REQUIRE(aliased_hover.front().find(
                    "count (alias of settings.sharedCount)") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///qualified-hover.janus","text":"import settings\n\ndef main() : int { return settings.sharedCount }"}}})"));
  const std::vector<std::string> qualified_hover = server.handle(
      R"({"jsonrpc":"2.0","id":58,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///qualified-hover.janus"},"position":{"line":2,"character":35}}})");
  require_hover_result(qualified_hover, "val sharedCount : int", 2, 35, 46);
  JANUS_REQUIRE(qualified_hover.front().find(
                    "settings.sharedCount (alias of settings.sharedCount)") !=
                std::string::npos);
  require_null_result(server.handle(
      R"({"jsonrpc":"2.0","id":60,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///qualified-hover.janus"},"position":{"line":2,"character":34}}})"));
  require_null_result(server.handle(
      R"({"jsonrpc":"2.0","id":61,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///qualified-hover.janus"},"position":{"line":2,"character":46}}})"));

  const std::vector<std::string> global_definition = server.handle(
      R"({"jsonrpc":"2.0","id":11,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":2,"character":30}}})");
  JANUS_REQUIRE(global_definition.front().find(
                    "\"uri\":\"file:///settings.janus\"") != std::string::npos);
  JANUS_REQUIRE(global_definition.front().find("\"line\":2") !=
                std::string::npos);

  const std::vector<std::string> global_references = server.handle(
      R"({"jsonrpc":"2.0","id":12,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":2,"character":30},"context":{"includeDeclaration":true}}})");
  JANUS_REQUIRE(global_references.front().find("file:///settings.janus") !=
                std::string::npos);
  JANUS_REQUIRE(global_references.front().find("file:///broken.janus") !=
                std::string::npos);

  const std::vector<std::string> global_completion = server.handle(
      R"({"jsonrpc":"2.0","id":13,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":18}}})");
  JANUS_REQUIRE(global_completion.front().find("\"label\":\"sharedCount\"") !=
                std::string::npos);
  JANUS_REQUIRE(global_completion.front().find("\"label\":\"secretCount\"") ==
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { return settings. }"}]}})"));
  const std::vector<std::string> module_completion = server.handle(
      R"({"jsonrpc":"2.0","id":14,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":35}}})");
  JANUS_REQUIRE(module_completion.front().find("\"label\":\"sharedCount\"") !=
                std::string::npos);
  JANUS_REQUIRE(module_completion.front().find("\"label\":\"secretCount\"") ==
                std::string::npos);

  const std::vector<std::string> else_if_document = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def choose(first : bool, second : bool) : int {\n    if first {\n        val low : int = 1\n        return low\n    } else if second {\n        val middle : int = 2\n        return middle\n    } else {\n        val high : int = 3\n        return high\n    }\n}\ndef main() : int { return choose(true, false) }"}]}})");
  JANUS_REQUIRE(else_if_document.size() == 1);
  JANUS_REQUIRE(else_if_document.front().find("\"diagnostics\":[]") !=
                std::string::npos);
  const std::vector<std::string> low_definition = server.handle(
      R"({"jsonrpc":"2.0","id":18,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":3,"character":15}}})");
  JANUS_REQUIRE(low_definition.front().find("\"line\":2") != std::string::npos);
  JANUS_REQUIRE(low_definition.front().find("\"character\":12") !=
                std::string::npos);
  const std::vector<std::string> middle_definition = server.handle(
      R"({"jsonrpc":"2.0","id":19,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":6,"character":15}}})");
  JANUS_REQUIRE(middle_definition.front().find("\"line\":5") !=
                std::string::npos);
  JANUS_REQUIRE(middle_definition.front().find("\"character\":12") !=
                std::string::npos);
  const std::vector<std::string> high_definition = server.handle(
      R"({"jsonrpc":"2.0","id":20,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":9,"character":15}}})");
  JANUS_REQUIRE(high_definition.front().find("\"line\":8") !=
                std::string::npos);
  JANUS_REQUIRE(high_definition.front().find("\"character\":12") !=
                std::string::npos);
  const std::vector<std::string> middle_references = server.handle(
      R"({"jsonrpc":"2.0","id":21,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":6,"character":15},"context":{"includeDeclaration":true}}})");
  std::size_t else_if_reference_count = 0;
  std::size_t else_if_reference_position = 0;
  while ((else_if_reference_position = middle_references.front().find(
              "\"uri\"", else_if_reference_position)) != std::string::npos) {
    ++else_if_reference_count;
    else_if_reference_position += 5;
  }
  JANUS_REQUIRE(else_if_reference_count == 2);

  const std::vector<std::string> signature_help = server.handle(
      R"({"jsonrpc":"2.0","id":22,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":12,"character":42}}})");
  JANUS_REQUIRE(
      signature_help.front().find(
          "\"label\":\"choose(first : bool, second : bool) : int\"") !=
      std::string::npos);
  JANUS_REQUIRE(signature_help.front().find("\"activeParameter\":1") !=
                std::string::npos);
  const std::vector<std::string> parameter_rename = server.handle(
      R"({"jsonrpc":"2.0","id":30,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":11},"newName":"primary"}})");
  JANUS_REQUIRE(parameter_rename.front().find("\"error\"") ==
                std::string::npos);
  JANUS_REQUIRE(parameter_rename.front().find("\"newText\":\"primary\"") !=
                std::string::npos);

  const std::vector<std::string> semantic_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":23,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///broken.janus"}}})");
  JANUS_REQUIRE(semantic_tokens.front().find("\"data\":[") !=
                std::string::npos);
  JANUS_REQUIRE(semantic_tokens.front().find("\"data\":[]") ==
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///integer-literals.janus","text":"def bits() : int { return 0xA2_0A + 0b1111_0000 + 1_000 }\n"}}})"));
  const std::string integer_semantic_tokens = require_lsp_result(
      server.handle(
          R"({"jsonrpc":"2.0","id":43,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///integer-literals.janus"}}})"),
      LspResultShape::SemanticTokens);
  JANUS_REQUIRE(semantic_token_type_at(integer_semantic_tokens, 0, 26) == 12);
  JANUS_REQUIRE(semantic_token_type_at(integer_semantic_tokens, 0, 36) == 12);
  JANUS_REQUIRE(semantic_token_type_at(integer_semantic_tokens, 0, 50) == 12);

  const std::vector<std::string> guarded_match_diagnostics = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///guarded-match.janus","text":"def decode(opcode : uint) : int { return match opcode { uint(8) if opcode == uint(8) => 8, _ => 0 } }\ndef main() : int { return decode(uint(8)) }\n"}}})");
  JANUS_REQUIRE(guarded_match_diagnostics.size() == 1);
  JANUS_REQUIRE(guarded_match_diagnostics.front().find("\"diagnostics\":[]") !=
                std::string::npos);
  const std::string guarded_match_tokens = require_lsp_result(
      server.handle(
          R"({"jsonrpc":"2.0","id":55,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///guarded-match.janus"}}})"),
      LspResultShape::SemanticTokens);
  JANUS_REQUIRE(semantic_token_type_at(guarded_match_tokens, 0, 61) == 12);
  JANUS_REQUIRE(semantic_token_type_at(guarded_match_tokens, 0, 64) == 10);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///bitwise.janus","text":"def bits(value : ubyte) : ubyte { return value << 1 | value & ubyte(3) ^ value >> 2 }\n"}}})"));
  const std::string bitwise_semantic_tokens = require_lsp_result(
      server.handle(
          R"({"jsonrpc":"2.0","id":54,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///bitwise.janus"}}})"),
      LspResultShape::SemanticTokens);
  JANUS_REQUIRE(semantic_token_type_at(bitwise_semantic_tokens, 0, 47) == 13);
  JANUS_REQUIRE(semantic_token_type_at(bitwise_semantic_tokens, 0, 52) == 13);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///pipeline.janus","text":"def increment(value : int) : int { return value + 1 }\ndef main() : int { return 41 |> increment }\n"}}})"));
  const std::string pipeline_semantic_tokens = require_lsp_result(
      server.handle(
          R"({"jsonrpc":"2.0","id":542,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///pipeline.janus"}}})"),
      LspResultShape::SemanticTokens);
  JANUS_REQUIRE(semantic_token_type_at(pipeline_semantic_tokens, 1, 29) == 13);

  const auto compound_diagnostics = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///compound.janus","text":"def main() : int { var value : int = 1 value += 2 value <<= 1 return value }\n"}}})");
  JANUS_REQUIRE(compound_diagnostics.front().find("\"diagnostics\":[]") !=
                std::string::npos);
  const std::string compound_tokens = require_lsp_result(
      server.handle(
          R"({"jsonrpc":"2.0","id":541,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///compound.janus"}}})"),
      LspResultShape::SemanticTokens);
  JANUS_REQUIRE(semantic_token_type_at(compound_tokens, 0, 45) == 13);
  JANUS_REQUIRE(semantic_token_type_at(compound_tokens, 0, 56) == 13);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-kinds.janus","text":"class C() { def f(x : C) : int { val local : C = x return local } }\ndef top(value : int) : int { return value }\ndef shadow(f : int) : int { return f() }\nprivate def hidden() : int { return 0 }\n"}}})"));
  const std::vector<std::string> classified_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":44,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-kinds.janus"}}})");
  const std::vector<std::int64_t> classified_types =
      semantic_token_field(classified_tokens.front(), 3);
  JANUS_REQUIRE(
      (classified_types ==
       std::vector<std::int64_t>{10, 2, 10, 6,  8, 2,  1,  10, 7, 2,  8,
                                 10, 7, 10, 5,  8, 1,  1,  10, 8, 10, 5,
                                 8,  1, 1,  10, 8, 10, 10, 5,  1, 10, 12}));
  const std::vector<std::int64_t> classified_modifiers =
      semantic_token_field(classified_tokens.front(), 4);
  JANUS_REQUIRE((classified_modifiers ==
                 std::vector<std::int64_t>{0, 1, 0, 1, 1, 0, 0, 0, 3, 0, 0,
                                           0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1,
                                           1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-qualified.janus","text":"import b\ndef main() : int { val x : b.Box = new b.Box() return b.helper() }\n"}}})"));
  const std::vector<std::string> qualified_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":45,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-qualified.janus"}}})");
  JANUS_REQUIRE((semantic_token_field(qualified_tokens.front(), 3) ==
                 std::vector<std::int64_t>{10, 0, 10, 5, 1, 10, 7, 0, 1, 10, 0,
                                           1, 10, 0, 5}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-shadowed-import.janus","text":"import b\ndef main(b : int) : int { return b.helper() }\n"}}})"));
  const std::vector<std::string> shadowed_import_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":46,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-shadowed-import.janus"}}})");
  JANUS_REQUIRE((semantic_token_field(shadowed_import_tokens.front(), 3) ==
                 std::vector<std::int64_t>{10, 0, 10, 5, 8, 1, 1, 10, 8, 6}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-import-path.janus","text":"import std.math\n"}}})"));
  const std::vector<std::string> import_path_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":47,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-import-path.janus"}}})");
  JANUS_REQUIRE((semantic_token_field(import_path_tokens.front(), 3) ==
                 std::vector<std::int64_t>{10, 0, 0}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-shadowed-path.janus","text":"import std.math\ndef main(std : int) : int { return std.math.gcd() }\n"}}})"));
  const std::vector<std::string> shadowed_path_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":48,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-shadowed-path.janus"}}})");
  JANUS_REQUIRE(
      (semantic_token_field(shadowed_path_tokens.front(), 3) ==
       std::vector<std::int64_t>{10, 0, 0, 10, 5, 8, 1, 1, 10, 8, 9, 6}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-callable.janus","text":"def invoke(f : (int) => int) : int { return f(1) }\n"}}})"));
  const std::vector<std::string> callable_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":49,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-callable.janus"}}})");
  JANUS_REQUIRE((semantic_token_field(callable_tokens.front(), 3) ==
                 std::vector<std::int64_t>{10, 5, 8, 1, 13, 1, 1, 10, 8,
                                           12}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-imported-generics.janus","text":"import model.{User, Box as Crate}\ndef inspect(users : Array[User], mapping : Map[string, User], nested : Array[Map[string, Crate]], qualified : Array[model.User], User : int) : int { val indexed = users[User] return User }\ndef call(User : int) : int { identity[User](User) return User }\ndef qualifiedCall(User : int) : int { model.make[model.User](User) return User }\n"}}})"));
  const std::vector<std::string> imported_generic_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":50,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-imported-generics.janus"}}})");
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 1, 26) ==
                1);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 1, 55) ==
                1);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 1, 89) ==
                1);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 1, 116) ==
                0);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 1, 122) ==
                1);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 1, 129) ==
                8);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 1, 169) ==
                8);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 1, 182) ==
                8);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 2, 38) ==
                1);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 2, 44) ==
                8);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 3, 49) ==
                0);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 3, 55) ==
                1);
  JANUS_REQUIRE(semantic_token_type_at(imported_generic_tokens.front(), 3, 61) ==
                8);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"class Box() { def get() : int { return 42 } }\ndef main() : int { val box = new Box() val inferred = box.get() delete box return inferred }"}]}})"));
  const std::vector<std::string> default_hints = server.handle(
      R"({"jsonrpc":"2.0","id":24,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///broken.janus"},"range":{"start":{"line":0,"character":0},"end":{"line":2,"character":0}}}})");
  JANUS_REQUIRE(default_hints.front().find("\": int\"") != std::string::npos);
  JANUS_REQUIRE(default_hints.front().find("\": Box\"") != std::string::npos);
  const std::vector<std::string> inferred_hover = server.handle(
      R"({"jsonrpc":"2.0","id":241,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":1,"character":82}}})");
  require_hover_result(inferred_hover, "val inferred : int", 1, 82, 90);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"workspace/didChangeConfiguration","params":{"settings":{"janus":{"inlayHints":{"inferredTypes":false}}}}})"));
  const std::vector<std::string> disabled_hints = server.handle(
      R"({"jsonrpc":"2.0","id":25,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///broken.janus"},"range":{"start":{"line":0,"character":0},"end":{"line":1,"character":0}}}})");
  JANUS_REQUIRE(disabled_hints.front().find("\"result\":[]") !=
                std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"workspace/didChangeConfiguration","params":{"settings":{"janus":{"inlayHints":{"inferredTypes":true}}}}})"));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///traits.janus","text":"trait Printable { def print() : int }\nclass Console() extends Printable { def print() : int { return 1 } }\ndef main() : int { return 0 }"}}})"));
  const std::vector<std::string> implementations = server.handle(
      R"({"jsonrpc":"2.0","id":26,"method":"textDocument/implementation","params":{"textDocument":{"uri":"file:///traits.janus"},"position":{"line":0,"character":7}}})");
  JANUS_REQUIRE(implementations.front().find("\"line\":1") !=
                std::string::npos);
  JANUS_REQUIRE(implementations.front().find("\"character\":6") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { val answer : int = 42 return answer }"}]}})"));
  const std::vector<std::string> prepare_rename = server.handle(
      R"({"jsonrpc":"2.0","id":27,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":50}}})");
  JANUS_REQUIRE(prepare_rename.front().find("\"placeholder\":\"answer\"") !=
                std::string::npos);
  const std::vector<std::string> local_rename = server.handle(
      R"({"jsonrpc":"2.0","id":28,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":50},"newName":"result"}})");
  JANUS_REQUIRE(local_rename.front().find("\"newText\":\"result\"") !=
                std::string::npos);
  JANUS_REQUIRE(local_rename.front().find("\"version\":1") !=
                std::string::npos);
  JANUS_REQUIRE(local_rename.front().find("\"uri\":\"file:///broken.janus\"") !=
                std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { val answer : int = 42 val result : int = answer return answer }"}]}})"));
  const std::vector<std::string> colliding_rename = server.handle(
      R"({"jsonrpc":"2.0","id":29,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":60},"newName":"result"}})");
  JANUS_REQUIRE(colliding_rename.front().find("\"error\"") !=
                std::string::npos);
  JANUS_REQUIRE(colliding_rename.front().find("\"changes\"") ==
                std::string::npos);
  const std::vector<std::string> keyword_rename = server.handle(
      R"({"jsonrpc":"2.0","id":34,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":60},"newName":"return"}})");
  JANUS_REQUIRE(keyword_rename.front().find("\"error\"") != std::string::npos);
  JANUS_REQUIRE(keyword_rename.front().find("\"documentChanges\"") ==
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///homonym_a.janus","text":"module homonym_a\n\ndef helper(value : int) : int { return value }\ntrait Printable { def print() : int }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///homonym_b.janus","text":"module homonym_b\n\ndef helper(text : string, enabled : bool, count : int, fallback : int) : int { return count }\ntrait Printable { def print() : int }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///homonym-consumer.janus","text":"import homonym_b\n\ndef main() : int {\n    return helper(\"a,b\", // ignored, comma\n                  true, 1, 0)\n}\n"}}})"));
  const std::vector<std::string> imported_homonym_definition = server.handle(
      R"({"jsonrpc":"2.0","id":31,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///homonym-consumer.janus"},"position":{"line":3,"character":13}}})");
  JANUS_REQUIRE(imported_homonym_definition.front().find(
                    "file:///homonym_b.janus") != std::string::npos);
  JANUS_REQUIRE(imported_homonym_definition.front().find(
                    "file:///homonym_a.janus") == std::string::npos);
  const std::vector<std::string> imported_homonym_references = server.handle(
      R"({"jsonrpc":"2.0","id":32,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///homonym-consumer.janus"},"position":{"line":3,"character":13},"context":{"includeDeclaration":true}}})");
  JANUS_REQUIRE(imported_homonym_references.front().find(
                    "file:///homonym_b.janus") != std::string::npos);
  JANUS_REQUIRE(imported_homonym_references.front().find(
                    "file:///homonym_a.janus") == std::string::npos);
  const std::vector<std::string> imported_homonym_signature = server.handle(
      R"({"jsonrpc":"2.0","id":33,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///homonym-consumer.janus"},"position":{"line":4,"character":22}}})");
  JANUS_REQUIRE(
      imported_homonym_signature.front().find(
          "helper(text : string, enabled : bool, count : int, fallback : "
          "int)") != std::string::npos);
  JANUS_REQUIRE(imported_homonym_signature.front().find(
                    "\"activeParameter\":1") != std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///unrelated-name.janus","text":"module missing_module\n\ndef accidental() : int { return 1 }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///missing-consumer.janus","text":"import missing_module\n\ndef main() : int { return accidental() }\n"}}})"));
  const std::vector<std::string> unresolved_file_import = server.handle(
      R"({"jsonrpc":"2.0","id":53,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///missing-consumer.janus"},"position":{"line":2,"character":30}}})");
  JANUS_REQUIRE(unresolved_file_import.front().find("\"result\":null") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///method-signature.janus","text":"class A() { def combine(value : string) : string { return value } }\nclass B() { def combine(left : int, right : int) : int { return left } }\n\ndef main() : int { val b : B = new B() return b.combine(1, 2) }\n"}}})"));
  const std::vector<std::string> method_signature = server.handle(
      R"({"jsonrpc":"2.0","id":42,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///method-signature.janus"},"position":{"line":3,"character":60}}})");
  JANUS_REQUIRE(
      method_signature.front().find("combine(left : int, right : int) : int") !=
      std::string::npos);
  JANUS_REQUIRE(method_signature.front().find("combine(value : string)") ==
                std::string::npos);
  JANUS_REQUIRE(method_signature.front().find("\"activeParameter\":1") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///method_a.janus","text":"module method_a\n\nclass Box() { def combine(value : string) : string { return value } }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///method_b.janus","text":"module method_b\n\nclass Box() { def combine(left : int, right : int) : int { return left } def combine(left : int, middle : int, right : int) : int { return middle } }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///method_c.janus","text":"module method_c\n\nclass Box() {}\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///qualified-method-consumer.janus","text":"import method_a\nimport method_b\n\ndef main() : int { val box : method_b.Box = new method_b.Box() return box.combine(1, 2) }\n"}}})"));
  const std::vector<std::string> qualified_method_signature = server.handle(
      R"({"jsonrpc":"2.0","id":43,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///qualified-method-consumer.janus"},"position":{"line":3,"character":85}}})");
  JANUS_REQUIRE(qualified_method_signature.front().find(
                    "combine(left : int, right : int) : int") !=
                std::string::npos);
  JANUS_REQUIRE(qualified_method_signature.front().find(
                    "combine(left : int, middle : int, right : int) : int") !=
                std::string::npos);
  JANUS_REQUIRE(qualified_method_signature.front().find(
                    "combine(value : string)") == std::string::npos);
  JANUS_REQUIRE(qualified_method_signature.front().find(
                    "\"activeParameter\":1") != std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///aliased-method-consumer.janus","text":"import method_a\nimport method_b as mb\n\ndef main() : int { val box : mb.Box = new mb.Box() return box.combine(1, 2) }\n"}}})"));
  const std::vector<std::string> aliased_method_signature = server.handle(
      R"({"jsonrpc":"2.0","id":43,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///aliased-method-consumer.janus"},"position":{"line":3,"character":73}}})");
  JANUS_REQUIRE(aliased_method_signature == qualified_method_signature);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///second-aliased-method-consumer.janus","text":"import method_b as boxes\n\ndef main() : int { val box : boxes.Box = new boxes.Box() return box.combine(1, 2) }\n"}}})"));
  const std::vector<std::string> second_aliased_method_signature =
      server.handle(
          R"({"jsonrpc":"2.0","id":43,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///second-aliased-method-consumer.janus"},"position":{"line":2,"character":79}}})");
  JANUS_REQUIRE(second_aliased_method_signature == qualified_method_signature);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///colliding-method-alias.janus","text":"import method_a as boxes\nimport method_b as boxes\n\ndef main() : int { val box : boxes.Box = new boxes.Box() return box.combine(1, 2) }\n"}}})"));
  const std::vector<std::string> colliding_method_signature = server.handle(
      R"({"jsonrpc":"2.0","id":44,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///colliding-method-alias.janus"},"position":{"line":3,"character":79}}})");
  JANUS_REQUIRE(colliding_method_signature.front().find("\"result\":null") !=
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///colliding-method-owner-alias.janus","text":"import method_b as boxes\nimport method_c as boxes\n\ndef main() : int { val box : boxes.Box = new boxes.Box() return box.combine(1, 2) }\n"}}})"));
  const std::vector<std::string> colliding_method_owner_signature =
      server.handle(
          R"({"jsonrpc":"2.0","id":45,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///colliding-method-owner-alias.janus"},"position":{"line":3,"character":79}}})");
  JANUS_REQUIRE(colliding_method_owner_signature.front().find(
                    "\"result\":null") != std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///homonym-consumer.janus"},"contentChanges":[{"text":"import homonym_b\n\nval foo : int = 7\ndef main() : int { return helper(1, true, 2, foo) }\n"}]}})"));
  const std::vector<std::string> captured_workspace_rename = server.handle(
      R"({"jsonrpc":"2.0","id":34,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///homonym_b.janus"},"position":{"line":2,"character":5},"newName":"foo"}})");
  JANUS_REQUIRE(captured_workspace_rename.front().find("\"error\"") !=
                std::string::npos);
  JANUS_REQUIRE(captured_workspace_rename.front().find("\"documentChanges\"") ==
                std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///trait-consumer.janus","text":"import homonym_b\n\nclass Console() extends Printable { def print() : int { return 1 } }\n"}}})"));
  const std::vector<std::string> imported_trait_implementations = server.handle(
      R"({"jsonrpc":"2.0","id":35,"method":"textDocument/implementation","params":{"textDocument":{"uri":"file:///homonym_b.janus"},"position":{"line":3,"character":7}}})");
  JANUS_REQUIRE(imported_trait_implementations.front().find(
                    "file:///trait-consumer.janus") != std::string::npos);
  const std::vector<std::string> unrelated_trait_implementations = server.handle(
      R"({"jsonrpc":"2.0","id":36,"method":"textDocument/implementation","params":{"textDocument":{"uri":"file:///homonym_a.janus"},"position":{"line":3,"character":7}}})");
  JANUS_REQUIRE(unrelated_trait_implementations.front().find(
                    "file:///trait-consumer.janus") == std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///utf16.janus","text":"def main() : int { val prefix = \"é😀\" val answer : int = 1 return answer }\n"}}})"));
  const std::vector<std::string> utf16_hover = server.handle(
      R"({"jsonrpc":"2.0","id":56,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///utf16.janus"},"position":{"line":0,"character":68}}})");
  require_hover_result(utf16_hover, "val answer : int", 0, 66, 72);
  const std::vector<std::string> utf16_definition = server.handle(
      R"({"jsonrpc":"2.0","id":37,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///utf16.janus"},"position":{"line":0,"character":66}}})");
  JANUS_REQUIRE(utf16_definition.front().find("\"character\":42") !=
                std::string::npos);
  const std::vector<std::string> utf16_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":38,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///utf16.janus"}}})");
  JANUS_REQUIRE(
      utf16_tokens.front().find("0,4,6,7,3,0,9,5,11,0,0,6,3,10,0,0,4,6,7,3") !=
      std::string::npos);

  const std::vector<std::string> utf16_diagnostics = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///utf16.janus"},"contentChanges":[{"text":"def main() : int { val prefix : string = \"é😀\" return missing }\n"}]}})");
  JANUS_REQUIRE(utf16_diagnostics.front().find(
                    "\"start\":{\"character\":54,\"line\":0}") !=
                std::string::npos);
  JANUS_REQUIRE(
      utf16_diagnostics.front().find("\"end\":{\"character\":55,\"line\":0}") !=
      std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///utf16.janus"},"contentChanges":[{"text":"def main() : int {\n    val first = 1\n    val ignored = \"😀\"\n    val second = 2\n    return first + second\n}\n"}]}})"));
  const std::vector<std::string> ranged_hints = server.handle(
      R"({"jsonrpc":"2.0","id":39,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///utf16.janus"},"range":{"start":{"line":3,"character":0},"end":{"line":4,"character":0}}}})");
  JANUS_REQUIRE(ranged_hints.front().find("\"line\":3") != std::string::npos);
  JANUS_REQUIRE(ranged_hints.front().find("\"line\":1") == std::string::npos);

  const std::vector<std::string> formatting = server.handle(
      R"({"jsonrpc":"2.0","id":6,"method":"textDocument/formatting","params":{"textDocument":{"uri":"file:///broken.janus"},"options":{"tabSize":2,"insertSpaces":true}}})");
  JANUS_REQUIRE(formatting.front().find("\"newText\"") != std::string::npos);

  const std::vector<std::string> array_diagnostics = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///array-literal.janus","text":"import std.array\n\ndef main() : int {\nval values : Array[int] = [\n1,\n2\n]\ndefer delete values\nreturn 0\n}\n"}}})");
  JANUS_REQUIRE(array_diagnostics.front().find("\"diagnostics\":[]") !=
                std::string::npos);
  const std::vector<std::string> array_formatting = server.handle(
      R"({"jsonrpc":"2.0","id":60,"method":"textDocument/formatting","params":{"textDocument":{"uri":"file:///array-literal.janus"},"options":{"tabSize":4,"insertSpaces":true}}})");
  JANUS_REQUIRE(array_formatting.front().find("        1,") !=
                std::string::npos);

  // Regression matrix for every advertised request that operates on source.
  TemporaryWorkspace temporary_workspace;
  const std::filesystem::path &workspace = temporary_workspace.path();
  janus::lsp::Server source_server{{std::filesystem::path{JANUS_STDLIB_DIR}}};
  const std::string indexed_uri = file_uri(workspace / "src/main.janus");
  const std::string unknown_uri = file_uri(workspace / "src/unknown.janus");
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"id\":100,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
      file_uri(workspace) + "\"}}"));
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_uri +
      "\",\"text\":\"def main() : int { val openValue = 1 return @0 }\\n\"}}}"));

  struct SourceResponses {
    std::string code_actions;
    std::string formatting;
    std::string semantic_tokens;
    std::string inlay_hints;
  };
  const auto source_requests = [&](const std::string &requested_uri,
                                   std::int64_t id) {
    SourceResponses responses;
    responses.code_actions = require_lsp_result(source_server.handle(
                           "{\"jsonrpc\":\"2.0\",\"id\":" +
                           std::to_string(id) +
                           ",\"method\":\"textDocument/codeAction\",\"params\":{\"textDocument\":{\"uri\":\"" +
                           requested_uri +
                           "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},\"context\":{\"diagnostics\":[]}}}"),
                       LspResultShape::CodeActions);
    responses.formatting = require_lsp_result(source_server.handle(
                           "{\"jsonrpc\":\"2.0\",\"id\":" +
                           std::to_string(id + 1) +
                           ",\"method\":\"textDocument/formatting\",\"params\":{\"textDocument\":{\"uri\":\"" +
                           requested_uri +
                           "\"},\"options\":{\"tabSize\":2,\"insertSpaces\":true}}}"),
                       LspResultShape::FormattingEdits);
    responses.semantic_tokens = require_lsp_result(source_server.handle(
                           "{\"jsonrpc\":\"2.0\",\"id\":" +
                           std::to_string(id + 2) +
                           ",\"method\":\"textDocument/semanticTokens/full\",\"params\":{\"textDocument\":{\"uri\":\"" +
                           requested_uri + "\"}}}"),
                       LspResultShape::SemanticTokens);
    responses.inlay_hints = require_lsp_result(source_server.handle(
                           "{\"jsonrpc\":\"2.0\",\"id\":" +
                           std::to_string(id + 3) +
                           ",\"method\":\"textDocument/inlayHint\",\"params\":{\"textDocument\":{\"uri\":\"" +
                           requested_uri +
                           "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":1,\"character\":0}}}}"),
                       LspResultShape::InlayHints);
    return responses;
  };

  const auto require_nonempty_source_results = [](const SourceResponses &got,
                                                   std::string_view name) {
    require_safe_correction(got.code_actions);
    JANUS_REQUIRE(got.formatting.find(name) != std::string::npos);
    JANUS_REQUIRE(!semantic_token_field(got.semantic_tokens, 0).empty());
    JANUS_REQUIRE(got.inlay_hints.find("\"label\":\": int\"") !=
                  std::string::npos);
  };
  const auto require_empty_source_results = [](const SourceResponses &got) {
    JANUS_REQUIRE(got.code_actions.find("\"result\":[]") != std::string::npos);
    JANUS_REQUIRE(got.formatting.find("\"result\":[]") != std::string::npos);
    JANUS_REQUIRE(got.semantic_tokens.find("\"data\":[]") != std::string::npos);
    JANUS_REQUIRE(got.inlay_hints.find("\"result\":[]") != std::string::npos);
  };

  // Four methods x three states: open, closed/indexed, and unknown.
  const SourceResponses opened = source_requests(indexed_uri, 101);
  require_nonempty_source_results(opened, "openValue");
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_uri + "\"}}}"));
  const SourceResponses indexed = source_requests(indexed_uri, 105);
  require_nonempty_source_results(indexed, "diskValue");
  JANUS_REQUIRE(indexed.formatting != opened.formatting);
  JANUS_REQUIRE(indexed.semantic_tokens != opened.semantic_tokens);

  const std::string indexed_method_consumer_uri =
      file_uri(workspace / "src/indexed-method-consumer.janus");
  const std::string indexed_method_module_uri =
      file_uri(workspace / "src/indexed_method_b.janus");
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_module_uri +
      "\",\"text\":\"module indexed_method_b\\n\"}}}"));
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_module_uri + "\"}}}"));
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_consumer_uri +
      "\",\"text\":\"import indexed_method_b as mb\\n\\ndef main() : int { "
      "val box : mb.Box = new mb.Box() return box.combine(1, 2) }\\n\"}}}"));
  const std::vector<std::string> indexed_aliased_method_signature =
      source_server.handle(
          "{\"jsonrpc\":\"2.0\",\"id\":109,\"method\":\"textDocument/signatureHelp\",\"params\":{\"textDocument\":{\"uri\":\"" +
          indexed_method_consumer_uri +
          "\"},\"position\":{\"line\":2,\"character\":73}}}");
  JANUS_REQUIRE(indexed_aliased_method_signature.front().find(
                    "combine(left : int, right : int) : int") !=
                std::string::npos);
  JANUS_REQUIRE(indexed_aliased_method_signature.front().find(
                    "combine(left : int, middle : int, right : int) : int") !=
                std::string::npos);
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_module_uri +
      "\",\"text\":\"module indexed_method_b\\n\\n"
      "struct Box() { def combine(left : int, right : int) : int { return "
      "left + right } }\\n"
      "struct LiveBox() { def combine(left : int, right : int) : int { "
      "return left + right } }\\n\"}}}"));
  const std::vector<std::string> inferred_import_change = source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_consumer_uri +
      "\"},\"contentChanges\":[{\"text\":\"import indexed_method_b as mb\\n\\ndef main() : int { val box = new mb.LiveBox() return box.combine(1, 2) }\\n\"}]}}");
  JANUS_REQUIRE(!inferred_import_change.empty());
  JANUS_REQUIRE(inferred_import_change.front().find("\"diagnostics\":[]") !=
                std::string::npos);
  const std::vector<std::string> imported_inlay_hints = source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"id\":110,\"method\":\"textDocument/inlayHint\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_consumer_uri +
      "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":4,\"character\":0}}}}");
  JANUS_REQUIRE(imported_inlay_hints.front().find(
                    "\"label\":\": mb.LiveBox\"") != std::string::npos);

  const std::string buffer_only_uri =
      file_uri(workspace / "src/buffer_only.janus");
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
      buffer_only_uri +
      "\",\"text\":\"module buffer_only\\n\\ndef apply(callback : (int) => int) : int { return callback(41) }\\n\"}}}"));
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_consumer_uri +
      "\"},\"contentChanges\":[{\"text\":\"import buffer_only as bo\\n\\ndef main() : int { val result = bo.apply(value => value + 1) return result }\\n\"}]}}"));
  const std::vector<std::string> buffer_only_hints = source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"id\":111,\"method\":\"textDocument/inlayHint\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_consumer_uri +
      "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":4,\"character\":0}}}}");
  JANUS_REQUIRE(buffer_only_hints.front().find("\"label\":\": int\"") !=
                std::string::npos);
  JANUS_REQUIRE(buffer_only_hints.front().find(
                    "\"position\":{\"character\":46,\"line\":2}") !=
                std::string::npos);
  const std::vector<std::string> lambda_parameter_hover = source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"id\":1111,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_consumer_uri +
      "\"},\"position\":{\"line\":2,\"character\":43}}}");
  JANUS_REQUIRE(lambda_parameter_hover.front().find(
                    "parameter value : int") != std::string::npos);
  const std::vector<std::string> buffer_only_hover = source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"id\":112,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_consumer_uri +
      "\"},\"position\":{\"line\":2,\"character\":70}}}");
  JANUS_REQUIRE(buffer_only_hover.front().find("result : int") !=
                std::string::npos);
  static_cast<void>(source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":\"" +
      buffer_only_uri + "\"}}}"));
  const std::vector<std::string> closed_buffer_hover = source_server.handle(
      "{\"jsonrpc\":\"2.0\",\"id\":113,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":\"" +
      indexed_method_consumer_uri +
      "\"},\"position\":{\"line\":2,\"character\":70}}}");
  JANUS_REQUIRE(closed_buffer_hover.front().find("result : int") ==
                std::string::npos);

  janus::lsp::Server expression_body_server;
  static_cast<void>(expression_body_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///expression-body.janus","text":"def square(value : int) : int => value * value\ndef use() : int => square(6)\n"}}})"));
  const std::vector<std::string> expression_symbols =
      expression_body_server.handle(
          R"({"jsonrpc":"2.0","id":201,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///expression-body.janus"}}})");
  JANUS_REQUIRE(expression_symbols.size() == 1);
  JANUS_REQUIRE(expression_symbols.front().find("\"name\":\"square\"") !=
                std::string::npos);
  JANUS_REQUIRE(expression_symbols.front().find("\"name\":\"use\"") !=
                std::string::npos);
  janus::lsp::Server constant_symbol_server;
  static_cast<void>(constant_symbol_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///constant-symbol.janus","text":"const declarationText : string = \"def misleading\"\n"}}})"));
  const std::vector<std::string> constant_symbols =
      constant_symbol_server.handle(
          R"({"jsonrpc":"2.0","id":210,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///constant-symbol.janus"}}})");
  JANUS_REQUIRE(constant_symbols.size() == 1);
  llvm::Expected<llvm::json::Value> parsed_constant_symbols =
      llvm::json::parse(constant_symbols.front());
  JANUS_REQUIRE(static_cast<bool>(parsed_constant_symbols));
  const llvm::json::Array *constant_symbol_items =
      parsed_constant_symbols->getAsObject()->getArray("result");
  JANUS_REQUIRE(constant_symbol_items != nullptr &&
                constant_symbol_items->size() == 1);
  JANUS_REQUIRE(constant_symbol_items->front().getAsObject()->getInteger(
                    "kind") == 14);
  janus::lsp::Server symbol_kind_server;
  static_cast<void>(symbol_kind_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///symbol-kinds.janus","text":"const c : int = 1\nval v : int = 2\nvar mutable : int = 3\ndef f() : int => 1\nclass C() { def method() : int => 1 }\nstruct S(value : int)\ntrait T { def required() : int }\nenum E { A }\n"}}})"));
  const std::string symbol_kinds = symbol_kind_server.handle(
      R"({"jsonrpc":"2.0","id":211,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///symbol-kinds.janus"}}})").front();
  llvm::Expected<llvm::json::Value> parsed_symbol_kinds =
      llvm::json::parse(symbol_kinds);
  JANUS_REQUIRE(static_cast<bool>(parsed_symbol_kinds));
  const llvm::json::Array *symbol_kind_items =
      parsed_symbol_kinds->getAsObject()->getArray("result");
  JANUS_REQUIRE(symbol_kind_items != nullptr);
  const auto expect_symbol_kind = [&](llvm::StringRef name,
                                      std::int64_t expected_kind) {
    for (const llvm::json::Value &item : *symbol_kind_items)
      if (const llvm::json::Object *object = item.getAsObject();
          object != nullptr && object->getString("name") == name) {
        JANUS_REQUIRE(object->getInteger("kind") == expected_kind);
        return;
      }
    JANUS_REQUIRE(false);
  };
  expect_symbol_kind("c", 14);
  expect_symbol_kind("v", 13);
  expect_symbol_kind("mutable", 13);
  expect_symbol_kind("f", 12);
  expect_symbol_kind("C", 5);
  expect_symbol_kind("method", 6);
  expect_symbol_kind("S", 23);
  expect_symbol_kind("T", 11);
  expect_symbol_kind("E", 10);
  const std::vector<std::string> expression_hover = expression_body_server.handle(
      R"({"jsonrpc":"2.0","id":202,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///expression-body.janus"},"position":{"line":1,"character":20}}})");
  JANUS_REQUIRE(expression_hover.front().find(
                    "square(value : int) : int") != std::string::npos);
  const std::vector<std::string> expression_signature =
      expression_body_server.handle(
          R"({"jsonrpc":"2.0","id":203,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///expression-body.janus"},"position":{"line":1,"character":26}}})");
  JANUS_REQUIRE(expression_signature.front().find(
                    "square(value : int) : int") != std::string::npos);
  const std::vector<std::string> expression_folding = expression_body_server.handle(
      R"({"jsonrpc":"2.0","id":204,"method":"textDocument/foldingRange","params":{"textDocument":{"uri":"file:///expression-body.janus"}}})");
  JANUS_REQUIRE(expression_folding.size() == 1);
  JANUS_REQUIRE(expression_folding.front().find("\"startLine\":0") ==
                std::string::npos);
  const std::vector<std::string> expression_selection =
      expression_body_server.handle(
          R"({"jsonrpc":"2.0","id":205,"method":"textDocument/selectionRange","params":{"textDocument":{"uri":"file:///expression-body.janus"},"positions":[{"line":0,"character":39}]}})");
  JANUS_REQUIRE(expression_selection.size() == 1);
  JANUS_REQUIRE(expression_selection.front().find("\"parent\"") !=
                std::string::npos);
  const std::string expression_tokens = require_lsp_result(
      expression_body_server.handle(
          R"({"jsonrpc":"2.0","id":206,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///expression-body.janus"}}})"),
      LspResultShape::SemanticTokens);
  JANUS_REQUIRE(semantic_token_type_at(expression_tokens, 0, 30) == 13);

  janus::lsp::Server expression_range_server;
  static_cast<void>(expression_range_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///expression-ranges.janus","text":"def callback() : (int) => int =>\n    (x : int) =>\n        x +\n        1\nclass Box() {\n    def method(value : int) : int =>\n        match value {\n            0 => 1,\n            _ => value\n        }\n}\ndef single() : int => 1\ndef unicode() : string => \"café\"\n"}}})"));
  const std::string exact_folding = expression_range_server.handle(
      R"({"jsonrpc":"2.0","id":207,"method":"textDocument/foldingRange","params":{"textDocument":{"uri":"file:///expression-ranges.janus"}}})").front();
  if (exact_folding.find("\"startLine\":0") == std::string::npos)
    std::cerr << "unexpected expression folding response: " << exact_folding
              << '\n';
  JANUS_REQUIRE(exact_folding.find("\"startLine\":0") != std::string::npos);
  JANUS_REQUIRE(exact_folding.find("\"endLine\":3") != std::string::npos);
  JANUS_REQUIRE(exact_folding.find("\"startLine\":5") != std::string::npos);
  JANUS_REQUIRE(exact_folding.find("\"endLine\":9") != std::string::npos);
  JANUS_REQUIRE(exact_folding.find("\"startLine\":11") == std::string::npos);
  const std::string exact_symbols = expression_range_server.handle(
      R"({"jsonrpc":"2.0","id":208,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///expression-ranges.janus"}}})").front();
  if (exact_symbols.find("\"end\":{\"character\":9,\"line\":3}") ==
      std::string::npos)
    std::cerr << "unexpected expression symbols response: " << exact_symbols
              << '\n';
  JANUS_REQUIRE(exact_symbols.find("\"end\":{\"character\":9,\"line\":3}") != std::string::npos);
  JANUS_REQUIRE(exact_symbols.find("\"name\":\"method\"") !=
                std::string::npos);
  JANUS_REQUIRE(exact_symbols.find("\"kind\":6") != std::string::npos);
  janus::lsp::Server utf_selection_server;
  static_cast<void>(utf_selection_server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///utf-selection.janus","text":"def emoji() : string => \"😀café\"\ndef number() : int => 42\n"}}})"));
  const std::string utf_selection = utf_selection_server.handle(
      R"({"jsonrpc":"2.0","id":209,"method":"textDocument/selectionRange","params":{"textDocument":{"uri":"file:///utf-selection.janus"},"positions":[{"line":0,"character":28},{"line":1,"character":22}]}})").front();
  llvm::Expected<llvm::json::Value> parsed_utf_selection =
      llvm::json::parse(utf_selection);
  JANUS_REQUIRE(static_cast<bool>(parsed_utf_selection));
  JANUS_REQUIRE(parsed_utf_selection->getAsObject()->getArray("result")->size() ==
                2);
  JANUS_REQUIRE(utf_selection.find("\"start\":{\"character\":24,\"line\":0}") != std::string::npos);
  JANUS_REQUIRE(utf_selection.find("\"end\":{\"character\":32,\"line\":0}") != std::string::npos);
  JANUS_REQUIRE(utf_selection.find("\"parent\"") != std::string::npos);
  const std::string expression_end_selection = utf_selection_server.handle(
      R"({"jsonrpc":"2.0","id":212,"method":"textDocument/selectionRange","params":{"textDocument":{"uri":"file:///utf-selection.janus"},"positions":[{"line":1,"character":24}]}})").front();
  JANUS_REQUIRE(expression_end_selection.find(
                    "\"start\":{\"character\":22,\"line\":1},\"end\":{\"character\":24,\"line\":1}}}") ==
                std::string::npos);

  const SourceResponses unknown = source_requests(unknown_uri, 109);
  require_empty_source_results(unknown);

  // Created after initialize: read from disk without growing the index.
  const std::filesystem::path direct_path =
      workspace / "src/janus direct # source.janus";
  TemporaryWorkspace::write(
      direct_path, "def direct() : int { val directValue = 7 return @0 }\n");
  const std::size_t indexed_files_before =
      source_server.workspace_index_metrics().files;
  const SourceResponses direct = source_requests(file_uri(direct_path), 113);
  require_nonempty_source_results(direct, "directValue");
  JANUS_REQUIRE(source_server.workspace_index_metrics().files ==
                indexed_files_before);
#ifndef _WIN32
  std::string localhost_uri = file_uri(direct_path);
  localhost_uri.insert(std::string{"file://"}.size(), "localhost");
  const SourceResponses localhost = source_requests(localhost_uri, 117);
  require_nonempty_source_results(localhost, "directValue");
  std::string foreign_uri = file_uri(direct_path);
  foreign_uri.insert(std::string{"file://"}.size(), "remote-host");
  const SourceResponses foreign = source_requests(foreign_uri, 121);
  require_empty_source_results(foreign);
#else
  JANUS_REQUIRE(file_uri(direct_path).starts_with("file:///"));
#endif

  // Neither an out-of-root .janus file nor an in-root non-source is read.
  const std::filesystem::path outside_path =
      temporary_workspace.outside("outside secret.janus");
  TemporaryWorkspace::write(
      outside_path, "def leaked() : int { val outsideSecret = 9 return @0 }\n");
  require_empty_source_results(source_requests(file_uri(outside_path), 125));
  const std::filesystem::path non_source_path = workspace / "src/secret.txt";
  TemporaryWorkspace::write(
      non_source_path, "def leaked() : int { val extensionSecret = 9 return @0 }\n");
  require_empty_source_results(source_requests(file_uri(non_source_path), 129));
#ifndef _WIN32
  // The workspace index may have seen the symlink, but source requests must
  // still reject its canonical target outside the authorized workspace.
  const std::filesystem::path cached_symlink =
      workspace / "src/cached-link.janus";
  require_empty_source_results(
      source_requests(file_uri(cached_symlink), 133));
#endif

  const std::filesystem::path later_search_path =
      temporary_workspace.outside("later-search-path");
  std::filesystem::create_directory(later_search_path);
  TemporaryWorkspace::write(
      later_search_path / "priority.janus",
      "module priority\n\ndef selected() : int { return 2 }\n");
  janus::lsp::Server import_priority_server{{later_search_path}};
  const std::string buffered_module_uri =
      file_uri(workspace / "src/priority.janus");
  const std::string canonical_buffered_module_uri = file_uri(
      std::filesystem::weakly_canonical(workspace / "src/priority.janus"));
  const std::string priority_consumer_uri =
      file_uri(workspace / "src/priority-consumer.janus");
  static_cast<void>(import_priority_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
      buffered_module_uri +
      "\",\"text\":\"module priority\\n\\ndef selected() : int { return 1 }\\n\"}}}"));
  static_cast<void>(import_priority_server.handle(
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
      priority_consumer_uri +
      "\",\"text\":\"import priority\\n\\ndef main() : int { return selected() }\\n\"}}}"));
  const std::vector<std::string> buffered_import_definition =
      import_priority_server.handle(
          "{\"jsonrpc\":\"2.0\",\"id\":137,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"" +
          priority_consumer_uri +
          "\"},\"position\":{\"line\":2,\"character\":31}}}");
  JANUS_REQUIRE(buffered_import_definition.front().find(
                    canonical_buffered_module_uri) != std::string::npos);
}
