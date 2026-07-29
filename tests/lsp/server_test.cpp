#include "janus/lsp/server.hpp"

#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

[[noreturn]] inline void janus_test_assertion_failed(const char *expression,
                                                     const char *file,
                                                     int line) {
  std::fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line,
               expression);
  std::abort();
}
#define REQUIRE(condition)                                                      \
  ((condition) ? static_cast<void>(0)                                           \
               : janus_test_assertion_failed(#condition, __FILE__, __LINE__))

namespace {

std::vector<std::int64_t> semantic_token_field(const std::string &response,
                                               std::size_t field) {
  REQUIRE(field < 5);
  const std::string marker = "\"data\":[";
  std::size_t cursor = response.find(marker);
  REQUIRE(cursor != std::string::npos);
  cursor += marker.size();
  std::vector<std::int64_t> values;
  while (cursor < response.size() && response[cursor] != ']') {
    std::int64_t value = 0;
    const char *begin = response.data() + cursor;
    const char *end = response.data() + response.size();
    const auto parsed = std::from_chars(begin, end, value);
    REQUIRE(parsed.ec == std::errc{});
    values.push_back(value);
    cursor = static_cast<std::size_t>(parsed.ptr - response.data());
    if (cursor < response.size() && response[cursor] == ',')
      ++cursor;
  }
  REQUIRE(values.size() % 5 == 0);
  std::vector<std::int64_t> fields;
  for (std::size_t index = field; index < values.size(); index += 5)
    fields.push_back(values[index]);
  return fields;
}

} // namespace

int main() {
  janus::lsp::Server server{{std::filesystem::path{JANUS_STDLIB_DIR}}};

  const std::vector<std::string> initialized = server.handle(
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
  assert(initialized.size() == 1);
  assert(initialized.front().find("\"textDocumentSync\"") !=
         std::string::npos);
  assert(initialized.front().find("\"renameProvider\"") != std::string::npos);
  assert(initialized.front().find("\"prepareProvider\":true") !=
         std::string::npos);
  assert(initialized.front().find("\"signatureHelpProvider\"") !=
         std::string::npos);
  assert(initialized.front().find("\"semanticTokensProvider\"") !=
         std::string::npos);
  assert(initialized.front().find("\"inlayHintProvider\":true") !=
         std::string::npos);
  assert(initialized.front().find("\"implementationProvider\":true") !=
         std::string::npos);

  const std::vector<std::string> invalid = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///broken.janus","version":1,"text":"def main() : int { return nope }"}}})");
  assert(invalid.size() == 1);
  assert(invalid.front().find("publishDiagnostics") != std::string::npos);
  assert(invalid.front().find("unknown value") != std::string::npos);
  assert(invalid.front().find("\"code\":\"JANA0001\"") !=
         std::string::npos);
  assert(invalid.front().find("\"severity\":1") != std::string::npos);

  const std::vector<std::string> recovered = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///recovery.janus","text":"def first() : int {\n val x : int = }\ndef second() : int {\n val y : int = }\n"}}})");
  assert(recovered.size() == 1);
  std::size_t parser_diagnostic_count = 0;
  std::size_t parser_diagnostic_position = 0;
  while ((parser_diagnostic_position = recovered.front().find(
              "\"code\":\"JPAR0001\"", parser_diagnostic_position)) !=
         std::string::npos) {
    ++parser_diagnostic_count;
    parser_diagnostic_position += 20;
  }
  assert(parser_diagnostic_count == 2);

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
  const std::vector<std::string> imported_definition = server.handle(
      R"({"jsonrpc":"2.0","id":15,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///array.janus"},"position":{"line":3,"character":20}}})");
  assert(imported_definition.front().find("stdlib/std/array.janus") !=
         std::string::npos);

  const std::vector<std::string> derivation_document = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///derivation.janus","text":"struct Point(val x : int, val y : int) derives Copy, Equality, Hashing, Debug {}\n\ndef main() : int { return 0 }"}}})");
  assert(derivation_document.size() == 1);
  assert(derivation_document.front().find("\"diagnostics\":[]") !=
         std::string::npos);

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

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"val answer : int = 1\ndef main() : int { val answer : int = 2 return answer }"}]}})"));
  const std::vector<std::string> shadowed_definition = server.handle(
      R"({"jsonrpc":"2.0","id":16,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":1,"character":49}}})");
  assert(shadowed_definition.front().find("\"line\":1") != std::string::npos);
  assert(shadowed_definition.front().find("\"character\":23") !=
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
  assert(reference_count == 2);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { val answer : int = 42 return answer }"}]}})"));

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
  assert(completion.front().find("\"label\":\"derives\"") !=
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///settings.janus","text":"module settings\n\nval sharedCount : int = 42\nprivate val secretCount : int = 7\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"import settings\n\ndef main() : int { return sharedCount }"}]}})"));

  const std::vector<std::string> global_hover = server.handle(
      R"({"jsonrpc":"2.0","id":10,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":2,"character":30}}})");
  assert(global_hover.front().find("val sharedCount : int") !=
         std::string::npos);
  assert(global_hover.front().find("module `settings`") != std::string::npos);

  const std::vector<std::string> global_definition = server.handle(
      R"({"jsonrpc":"2.0","id":11,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":2,"character":30}}})");
  assert(global_definition.front().find("\"uri\":\"file:///settings.janus\"") !=
         std::string::npos);
  assert(global_definition.front().find("\"line\":2") != std::string::npos);

  const std::vector<std::string> global_references = server.handle(
      R"({"jsonrpc":"2.0","id":12,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":2,"character":30},"context":{"includeDeclaration":true}}})");
  assert(global_references.front().find("file:///settings.janus") !=
         std::string::npos);
  assert(global_references.front().find("file:///broken.janus") !=
         std::string::npos);

  const std::vector<std::string> global_completion = server.handle(
      R"({"jsonrpc":"2.0","id":13,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":18}}})");
  assert(global_completion.front().find("\"label\":\"sharedCount\"") !=
         std::string::npos);
  assert(global_completion.front().find("\"label\":\"secretCount\"") ==
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { return settings. }"}]}})"));
  const std::vector<std::string> module_completion = server.handle(
      R"({"jsonrpc":"2.0","id":14,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":35}}})");
  assert(module_completion.front().find("\"label\":\"sharedCount\"") !=
         std::string::npos);
  assert(module_completion.front().find("\"label\":\"secretCount\"") ==
         std::string::npos);

  const std::vector<std::string> else_if_document = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def choose(first : bool, second : bool) : int {\n    if first {\n        val low : int = 1\n        return low\n    } else if second {\n        val middle : int = 2\n        return middle\n    } else {\n        val high : int = 3\n        return high\n    }\n}\ndef main() : int { return choose(true, false) }"}]}})");
  assert(else_if_document.size() == 1);
  assert(else_if_document.front().find("\"diagnostics\":[]") !=
         std::string::npos);
  const std::vector<std::string> low_definition = server.handle(
      R"({"jsonrpc":"2.0","id":18,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":3,"character":15}}})");
  assert(low_definition.front().find("\"line\":2") != std::string::npos);
  assert(low_definition.front().find("\"character\":12") !=
         std::string::npos);
  const std::vector<std::string> middle_definition = server.handle(
      R"({"jsonrpc":"2.0","id":19,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":6,"character":15}}})");
  assert(middle_definition.front().find("\"line\":5") != std::string::npos);
  assert(middle_definition.front().find("\"character\":12") !=
         std::string::npos);
  const std::vector<std::string> high_definition = server.handle(
      R"({"jsonrpc":"2.0","id":20,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":9,"character":15}}})");
  assert(high_definition.front().find("\"line\":8") != std::string::npos);
  assert(high_definition.front().find("\"character\":12") !=
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
  assert(else_if_reference_count == 2);

  const std::vector<std::string> signature_help = server.handle(
      R"({"jsonrpc":"2.0","id":22,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":12,"character":42}}})");
  assert(signature_help.front().find(
             "\"label\":\"choose(first : bool, second : bool) : int\"") !=
         std::string::npos);
  assert(signature_help.front().find("\"activeParameter\":1") !=
         std::string::npos);
  const std::vector<std::string> parameter_rename = server.handle(
      R"({"jsonrpc":"2.0","id":30,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":11},"newName":"primary"}})");
  assert(parameter_rename.front().find("\"error\"") == std::string::npos);
  assert(parameter_rename.front().find("\"newText\":\"primary\"") !=
         std::string::npos);

  const std::vector<std::string> semantic_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":23,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///broken.janus"}}})");
  assert(semantic_tokens.front().find("\"data\":[") != std::string::npos);
  assert(semantic_tokens.front().find("\"data\":[]") == std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-kinds.janus","text":"class C() { def f(x : C) : int { val local : C = x return local } }\ndef top(value : int) : int { return value }\ndef shadow(f : int) : int { return f() }\nprivate def hidden() : int { return 0 }\n"}}})"));
  const std::vector<std::string> classified_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":44,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-kinds.janus"}}})");
  const std::vector<std::int64_t> classified_types =
      semantic_token_field(classified_tokens.front(), 3);
  REQUIRE((classified_types ==
          std::vector<std::int64_t>{10, 2, 10, 6, 8, 2, 1, 10, 7, 2,
                                    8, 10, 7, 10, 5, 8, 1, 1, 10, 8,
                                    10, 5, 8, 1, 1, 10, 8, 10, 10, 5,
                                    1, 10, 12}));
  const std::vector<std::int64_t> classified_modifiers =
      semantic_token_field(classified_tokens.front(), 4);
  REQUIRE((classified_modifiers ==
          std::vector<std::int64_t>{0, 1, 0, 1, 1, 0, 0, 0, 3, 0,
                                    0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
                                    0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
                                    0, 0, 0}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-qualified.janus","text":"import b\ndef main() : int { val x : b.Box = new b.Box() return b.helper() }\n"}}})"));
  const std::vector<std::string> qualified_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":45,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-qualified.janus"}}})");
  REQUIRE((semantic_token_field(qualified_tokens.front(), 3) ==
          std::vector<std::int64_t>{10, 0, 10, 5, 1, 10, 7, 0, 1, 10,
                                    0, 1, 10, 0, 5}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-shadowed-import.janus","text":"import b\ndef main(b : int) : int { return b.helper() }\n"}}})"));
  const std::vector<std::string> shadowed_import_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":46,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-shadowed-import.janus"}}})");
  REQUIRE((semantic_token_field(shadowed_import_tokens.front(), 3) ==
          std::vector<std::int64_t>{10, 0, 10, 5, 8, 1, 1, 10, 8, 6}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-import-path.janus","text":"import std.math\n"}}})"));
  const std::vector<std::string> import_path_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":47,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-import-path.janus"}}})");
  REQUIRE((semantic_token_field(import_path_tokens.front(), 3) ==
          std::vector<std::int64_t>{10, 0, 0}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-shadowed-path.janus","text":"import std.math\ndef main(std : int) : int { return std.math.gcd() }\n"}}})"));
  const std::vector<std::string> shadowed_path_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":48,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-shadowed-path.janus"}}})");
  REQUIRE((semantic_token_field(shadowed_path_tokens.front(), 3) ==
          std::vector<std::int64_t>{10, 0, 0, 10, 5, 8, 1, 1, 10, 8, 9,
                                    6}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///semantic-callable.janus","text":"def invoke(f : (int) => int) : int { return f(1) }\n"}}})"));
  const std::vector<std::string> callable_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":49,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///semantic-callable.janus"}}})");
  REQUIRE((semantic_token_field(callable_tokens.front(), 3) ==
          std::vector<std::int64_t>{10, 5, 8, 1, 1, 1, 10, 8, 12}));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { val inferred = 42 return inferred }"}]}})"));
  const std::vector<std::string> default_hints = server.handle(
      R"({"jsonrpc":"2.0","id":24,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///broken.janus"},"range":{"start":{"line":0,"character":0},"end":{"line":1,"character":0}}}})");
  assert(default_hints.front().find("\": int\"") != std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"workspace/didChangeConfiguration","params":{"settings":{"janus":{"inlayHints":{"inferredTypes":false}}}}})"));
  const std::vector<std::string> disabled_hints = server.handle(
      R"({"jsonrpc":"2.0","id":25,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///broken.janus"},"range":{"start":{"line":0,"character":0},"end":{"line":1,"character":0}}}})");
  assert(disabled_hints.front().find("\"result\":[]") != std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"workspace/didChangeConfiguration","params":{"settings":{"janus":{"inlayHints":{"inferredTypes":true}}}}})"));

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///traits.janus","text":"trait Printable { def print() : int }\nclass Console() extends Printable { def print() : int { return 1 } }\ndef main() : int { return 0 }"}}})"));
  const std::vector<std::string> implementations = server.handle(
      R"({"jsonrpc":"2.0","id":26,"method":"textDocument/implementation","params":{"textDocument":{"uri":"file:///traits.janus"},"position":{"line":0,"character":7}}})");
  assert(implementations.front().find("\"line\":1") != std::string::npos);
  assert(implementations.front().find("\"character\":6") !=
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { val answer : int = 42 return answer }"}]}})"));
  const std::vector<std::string> prepare_rename = server.handle(
      R"({"jsonrpc":"2.0","id":27,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":50}}})");
  assert(prepare_rename.front().find("\"placeholder\":\"answer\"") !=
         std::string::npos);
  const std::vector<std::string> local_rename = server.handle(
      R"({"jsonrpc":"2.0","id":28,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":50},"newName":"result"}})");
  assert(local_rename.front().find("\"newText\":\"result\"") !=
         std::string::npos);
  assert(local_rename.front().find("\"version\":1") != std::string::npos);
  assert(local_rename.front().find("\"uri\":\"file:///broken.janus\"") !=
         std::string::npos);
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///broken.janus"},"contentChanges":[{"text":"def main() : int { val answer : int = 42 val result : int = answer return answer }"}]}})"));
  const std::vector<std::string> colliding_rename = server.handle(
      R"({"jsonrpc":"2.0","id":29,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":60},"newName":"result"}})");
  assert(colliding_rename.front().find("\"error\"") != std::string::npos);
  assert(colliding_rename.front().find("\"changes\"") == std::string::npos);
  const std::vector<std::string> keyword_rename = server.handle(
      R"({"jsonrpc":"2.0","id":34,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///broken.janus"},"position":{"line":0,"character":60},"newName":"return"}})");
  assert(keyword_rename.front().find("\"error\"") != std::string::npos);
  assert(keyword_rename.front().find("\"documentChanges\"") ==
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///homonym-a.janus","text":"module homonym_a\n\ndef helper(value : int) : int { return value }\ntrait Printable { def print() : int }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///homonym-b.janus","text":"module homonym_b\n\ndef helper(text : string, enabled : bool, count : int, fallback : int) : int { return count }\ntrait Printable { def print() : int }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///homonym-consumer.janus","text":"import homonym_b\n\ndef main() : int {\n    return helper(\"a,b\", // ignored, comma\n                  true, 1, 0)\n}\n"}}})"));
  const std::vector<std::string> imported_homonym_definition = server.handle(
      R"({"jsonrpc":"2.0","id":31,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///homonym-consumer.janus"},"position":{"line":3,"character":13}}})");
  assert(imported_homonym_definition.front().find("file:///homonym-b.janus") !=
         std::string::npos);
  assert(imported_homonym_definition.front().find("file:///homonym-a.janus") ==
         std::string::npos);
  const std::vector<std::string> imported_homonym_references = server.handle(
      R"({"jsonrpc":"2.0","id":32,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///homonym-consumer.janus"},"position":{"line":3,"character":13},"context":{"includeDeclaration":true}}})");
  assert(imported_homonym_references.front().find("file:///homonym-b.janus") !=
         std::string::npos);
  assert(imported_homonym_references.front().find("file:///homonym-a.janus") ==
         std::string::npos);
  const std::vector<std::string> imported_homonym_signature = server.handle(
      R"({"jsonrpc":"2.0","id":33,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///homonym-consumer.janus"},"position":{"line":4,"character":22}}})");
  assert(imported_homonym_signature.front().find(
             "helper(text : string, enabled : bool, count : int, fallback : int)") !=
         std::string::npos);
  assert(imported_homonym_signature.front().find("\"activeParameter\":1") !=
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///method-signature.janus","text":"class A() { def combine(value : string) : string { return value } }\nclass B() { def combine(left : int, right : int) : int { return left } }\n\ndef main() : int { val b : B = new B() return b.combine(1, 2) }\n"}}})"));
  const std::vector<std::string> method_signature = server.handle(
      R"({"jsonrpc":"2.0","id":42,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///method-signature.janus"},"position":{"line":3,"character":60}}})");
  assert(method_signature.front().find(
             "combine(left : int, right : int) : int") !=
         std::string::npos);
  assert(method_signature.front().find("combine(value : string)") ==
         std::string::npos);
  assert(method_signature.front().find("\"activeParameter\":1") !=
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///qualified-method-a.janus","text":"module method_a\n\nclass Box() { def combine(value : string) : string { return value } }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///qualified-method-b.janus","text":"module method_b\n\nclass Box() { def combine(left : int, right : int) : int { return left } }\n"}}})"));
  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///qualified-method-consumer.janus","text":"import method_a\nimport method_b\n\ndef main() : int { val box : method_b.Box = new method_b.Box() return box.combine(1, 2) }\n"}}})"));
  const std::vector<std::string> qualified_method_signature = server.handle(
      R"({"jsonrpc":"2.0","id":43,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///qualified-method-consumer.janus"},"position":{"line":3,"character":85}}})");
  assert(qualified_method_signature.front().find(
             "combine(left : int, right : int) : int") !=
         std::string::npos);
  assert(qualified_method_signature.front().find("combine(value : string)") ==
         std::string::npos);
  assert(qualified_method_signature.front().find("\"activeParameter\":1") !=
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///homonym-consumer.janus"},"contentChanges":[{"text":"import homonym_b\n\nval foo : int = 7\ndef main() : int { return helper(1, true, 2, foo) }\n"}]}})"));
  const std::vector<std::string> captured_workspace_rename = server.handle(
      R"({"jsonrpc":"2.0","id":34,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///homonym-b.janus"},"position":{"line":2,"character":5},"newName":"foo"}})");
  assert(captured_workspace_rename.front().find("\"error\"") !=
         std::string::npos);
  assert(captured_workspace_rename.front().find("\"documentChanges\"") ==
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///trait-consumer.janus","text":"import homonym_b\n\nclass Console() extends Printable { def print() : int { return 1 } }\n"}}})"));
  const std::vector<std::string> imported_trait_implementations =
      server.handle(
          R"({"jsonrpc":"2.0","id":35,"method":"textDocument/implementation","params":{"textDocument":{"uri":"file:///homonym-b.janus"},"position":{"line":3,"character":7}}})");
  assert(imported_trait_implementations.front().find(
             "file:///trait-consumer.janus") != std::string::npos);
  const std::vector<std::string> unrelated_trait_implementations =
      server.handle(
          R"({"jsonrpc":"2.0","id":36,"method":"textDocument/implementation","params":{"textDocument":{"uri":"file:///homonym-a.janus"},"position":{"line":3,"character":7}}})");
  assert(unrelated_trait_implementations.front().find(
             "file:///trait-consumer.janus") == std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///utf16.janus","text":"def main() : int { val prefix = \"é😀\" val answer : int = 1 return answer }\n"}}})"));
  const std::vector<std::string> utf16_definition = server.handle(
      R"({"jsonrpc":"2.0","id":37,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///utf16.janus"},"position":{"line":0,"character":66}}})");
  assert(utf16_definition.front().find("\"character\":42") !=
         std::string::npos);
  const std::vector<std::string> utf16_tokens = server.handle(
      R"({"jsonrpc":"2.0","id":38,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///utf16.janus"}}})");
  assert(utf16_tokens.front().find(
             "0,4,6,7,3,0,9,5,11,0,0,6,3,10,0,0,4,6,7,3") !=
         std::string::npos);

  const std::vector<std::string> utf16_diagnostics = server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///utf16.janus"},"contentChanges":[{"text":"def main() : int { val prefix : string = \"é😀\" return missing }\n"}]}})");
  assert(utf16_diagnostics.front().find(
             "\"start\":{\"character\":54,\"line\":0}") !=
         std::string::npos);
  assert(utf16_diagnostics.front().find(
             "\"end\":{\"character\":55,\"line\":0}") !=
         std::string::npos);

  static_cast<void>(server.handle(
      R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///utf16.janus"},"contentChanges":[{"text":"def main() : int {\n    val first = 1\n    val ignored = \"😀\"\n    val second = 2\n    return first + second\n}\n"}]}})"));
  const std::vector<std::string> ranged_hints = server.handle(
      R"({"jsonrpc":"2.0","id":39,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///utf16.janus"},"range":{"start":{"line":3,"character":0},"end":{"line":4,"character":0}}}})");
  assert(ranged_hints.front().find("\"line\":3") != std::string::npos);
  assert(ranged_hints.front().find("\"line\":1") == std::string::npos);

  const std::vector<std::string> formatting = server.handle(
      R"({"jsonrpc":"2.0","id":6,"method":"textDocument/formatting","params":{"textDocument":{"uri":"file:///broken.janus"},"options":{"tabSize":2,"insertSpaces":true}}})");
  assert(formatting.front().find("\"newText\"") != std::string::npos);
}
