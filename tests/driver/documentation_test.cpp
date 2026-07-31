#include "janus/driver/documentation.hpp"
#include "janus/frontend/parser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

std::string read(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

std::size_t count_occurrences(std::string_view haystack,
                              std::string_view needle) {
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = haystack.find(needle, position)) != std::string_view::npos) {
    ++count;
    position += needle.size();
  }
  return count;
}

} // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "janus-doc-test";
  std::filesystem::remove_all(root);

  janus::frontend::Parser parser{R"(
/// Package module linking to [[Widget]] and [[Missing]] with <unsafe> text.
/// @example
/// ```janus
/// val widget : Widget = Widget()
/// ```
module sample
/// Visible type.
struct Widget() {
    /// Visible field.
    val name : string = "ok"
    /// Hidden field.
    private val secret : int = 1
    /// Builds a label for [[Widget]].
    ///
    /// The prefix is escaped and kept next to [[Status]].
    /// @param prefix Text placed before the label and [[Widget]].
    /// @return The resulting <label> linked to [[Status]].
    /// @example
    /// ```janus
    /// val label = Widget().label("<unsafe>")
    /// ```
    def label(prefix : string) : string { return prefix }
    /// Hidden method.
    internal def reset() : int { return 0 }
}
/// A shade value.
/// @param intensity Initial shade intensity.
class Shade(intensity : int) {}
/// Creates a shade value.
/// @return A new [[Shade]].
def shade() : Shade { return new Shade(1) }
/// Visible state.
enum Status {
    /// Ready state.
    Ready,
    /// Failed state.
    Failed
}
/// Visible trait.
trait Printable {
    /// Render a value.
    /// @return The rendered value.
    def render() : string
}
/// Creates a widget.
/// @param unknown This parameter does not exist.
/// @param unknown This parameter is duplicated.
def create(name : string, count : int) : Widget { return Widget() }
/// Performs work.
def perform() : Unit {}
/// Hidden function.
private def hidden() : int { return 0 }
)"};
  std::vector<janus::ast::Program> programs;
  programs.push_back(parser.parse_program());

  const janus::driver::DocumentationReport first =
      janus::driver::generate_documentation(
          programs, {"sample-package", "1.2.3", root / "first"}, true);
  const janus::driver::DocumentationReport second =
      janus::driver::generate_documentation(
          programs, {"sample-package", "1.2.3", root / "second"}, true);

  const std::string html = read(first.index_path);
  const std::string api_index = read(first.api_index_path);
  expect(html == read(second.index_path), "HTML generation is deterministic");
  expect(html.find("<title>sample-package 1.2.3 API</title>") !=
             std::string::npos,
         "package metadata is rendered");
  expect(api_index.find("\"name\":\"sample\",\"kind\":\"module\"") !=
             std::string::npos,
         "modules are present in the public index");
  expect(html.find("Visible type.") != std::string::npos,
         "public type documentation is rendered");
  expect(html.find("Visible field.") != std::string::npos,
         "public fields are indexed");
  expect(html.find("Builds a label") != std::string::npos,
         "public methods are indexed");
  expect(html.find("Ready state.") != std::string::npos,
         "enum variants are indexed");
  expect(html.find("Visible trait.") != std::string::npos,
         "traits are indexed");
  expect(html.find("Creates a widget.") != std::string::npos,
         "functions are indexed");
  expect(html.find("Hidden field.") == std::string::npos,
         "private fields are excluded");
  expect(html.find("Hidden method.") == std::string::npos,
         "internal methods are excluded");
  expect(html.find("Hidden function.") == std::string::npos,
         "private functions are excluded");
  expect(count_occurrences(html, "id=\"sample-shade\"") == 1 &&
             count_occurrences(html, "id=\"sample-shade-function\"") == 1,
         "case-insensitive symbol collisions receive unique stable anchors");
  expect(api_index.find("\"name\":\"sample.Shade\"") != std::string::npos &&
             api_index.find("\"anchor\":\"sample-shade-function\"") !=
                 std::string::npos,
         "the API index exposes the disambiguated anchor");
  expect(html.find("class Shade(intensity : int)") != std::string::npos &&
             api_index.find(
                 "\"name\":\"intensity\",\"type\":\"int\",\"description\":\"Initial shade intensity.\"") !=
                 std::string::npos,
         "class constructor parameters are documented with their types");
  expect(html.find("href=\"#sample-widget\"") != std::string::npos,
         "known documentation links are resolved");
  expect(html.find("id=\"api-search\"") != std::string::npos &&
             html.find("Search modules, symbols and signatures") !=
                 std::string::npos,
         "Scala-inspired API search is rendered");
  expect(html.find("class=\"doc-layout\"") != std::string::npos &&
             html.find("class=\"module-nav\"") != std::string::npos &&
             html.find("class=\"symbol-card kind-struct\"") !=
                 std::string::npos,
         "API layout renders content cards and the module sidebar");
  expect(html.find("@media(max-width:800px)") != std::string::npos &&
             html.find("class=\"nav-toggle\"") != std::string::npos,
         "API layout includes responsive navigation");
  expect(html.find("No network resources are required") != std::string::npos &&
             html.find("https://") == std::string::npos,
         "API documentation remains self-contained and offline");
  expect(html.find("&lt;unsafe&gt;") != std::string::npos &&
             html.find("<unsafe>") == std::string::npos,
         "documentation text is HTML-escaped");
  expect(html.find("class=\"doc-summary\">Builds a label") !=
             std::string::npos &&
             html.find("class=\"doc-details\"") != std::string::npos,
         "summary and detail paragraphs are rendered separately");
  expect(html.find("class=\"module-example doc-section\"") !=
             std::string::npos &&
             html.find("<h3>Usage example</h3>") != std::string::npos &&
             html.find("val widget : Widget = Widget()") != std::string::npos,
         "module usage examples are visible before the public members");
  expect(html.find("<h4>Parameters</h4>") != std::string::npos &&
             html.find("<code>prefix</code>") != std::string::npos &&
             html.find("<code>string</code>") != std::string::npos,
         "real parameter names and types are rendered");
  expect(html.find("<h4>Returns</h4>") != std::string::npos &&
             html.find("The resulting &lt;label&gt;") != std::string::npos,
         "return type and escaped explanation are rendered");
  expect(html.find("<h4>Example</h4>") != std::string::npos &&
             html.find("&quot;&lt;unsafe&gt;&quot;") != std::string::npos,
         "examples are readable and strictly HTML-escaped");
  expect(api_index.find("\"summary\":\"Builds a label for [[Widget]].\"") !=
             std::string::npos &&
             api_index.find("\"details\":[\"The prefix is escaped") !=
                 std::string::npos &&
             api_index.find("\"parameters\":[{\"name\":\"prefix\",\"type\":\"string\"") !=
                 std::string::npos &&
             api_index.find("\"returns\":{\"type\":\"string\"") !=
                 std::string::npos &&
             api_index.find("\"examples\":[\"val label = Widget().label") !=
                 std::string::npos,
         "the compatible API index exposes deterministic structured fields");
  expect(api_index.find("\"returns\":{\"type\":\"Unit\"") ==
             std::string::npos,
         "canonical Unit returns do not require or expose return documentation");
  expect(html.find("href=\"#sample-widget\"") != std::string::npos &&
             html.find("href=\"#sample-status\"") != std::string::npos,
         "documentation references resolve in every structured section");
  expect(first.unresolved_links.size() == 1 &&
             first.unresolved_links[0].symbol == "Missing",
         "unknown documentation links are reported");
  expect(first.diagnostics.size() == 5,
         "documentation contracts produce one structured diagnostic per issue");
  expect(first.diagnostics[0].code == "duplicate-param" ||
             first.diagnostics[1].code == "duplicate-param",
         "duplicate parameter tags are diagnosed");
  bool saw_unknown = false;
  bool saw_missing_name = false;
  bool saw_missing_count = false;
  bool saw_missing_return = false;
  for (const auto &diagnostic : first.diagnostics) {
    saw_unknown |= diagnostic.code == "unknown-param" &&
                   diagnostic.parameter == "unknown";
    saw_missing_name |= diagnostic.code == "undocumented-param" &&
                        diagnostic.parameter == "name";
    saw_missing_count |= diagnostic.code == "undocumented-param" &&
                         diagnostic.parameter == "count";
    saw_missing_return |= diagnostic.code == "missing-return" &&
                          diagnostic.symbol == "sample.create";
  }
  expect(saw_unknown && saw_missing_name && saw_missing_count &&
             saw_missing_return,
         "unknown, undocumented parameters and missing returns are actionable");

  const std::filesystem::path blocked = root / "blocked";
  {
    std::ofstream blocker{blocked};
    blocker << "not a directory";
  }
  bool rejected_output = false;
  try {
    static_cast<void>(janus::driver::generate_documentation(
        programs, {"sample-package", "1.2.3", blocked}, true));
  } catch (const std::filesystem::filesystem_error &) {
    rejected_output = true;
  }
  expect(rejected_output, "invalid output paths are reported");

  std::filesystem::remove_all(root);
  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "offline API documentation is deterministic and public-only\n";
  return 0;
}
