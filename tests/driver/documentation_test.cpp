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

} // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "janus-doc-test";
  std::filesystem::remove_all(root);

  janus::frontend::Parser parser{R"(
/// Package module linking to [[Widget]] and [[Missing]] with <unsafe> text.
module sample
/// Visible type.
struct Widget() {
    /// Visible field.
    val name : string = "ok"
    /// Hidden field.
    private val secret : int = 1
    /// Visible method.
    def label() : string { return name }
    /// Hidden method.
    internal def reset() : int { return 0 }
}
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
    def render() : string
}
/// Visible function.
def create() : Widget { return Widget() }
/// Hidden function.
private def hidden() : int { return 0 }
)"};
  std::vector<janus::ast::Program> programs;
  programs.push_back(parser.parse_program());

  const janus::driver::DocumentationReport first =
      janus::driver::generate_documentation(
          programs, {"sample-package", "1.2.3", root / "first"});
  const janus::driver::DocumentationReport second =
      janus::driver::generate_documentation(
          programs, {"sample-package", "1.2.3", root / "second"});

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
  expect(html.find("Visible method.") != std::string::npos,
         "public methods are indexed");
  expect(html.find("Ready state.") != std::string::npos,
         "enum variants are indexed");
  expect(html.find("Visible trait.") != std::string::npos,
         "traits are indexed");
  expect(html.find("Visible function.") != std::string::npos,
         "functions are indexed");
  expect(html.find("Hidden field.") == std::string::npos,
         "private fields are excluded");
  expect(html.find("Hidden method.") == std::string::npos,
         "internal methods are excluded");
  expect(html.find("Hidden function.") == std::string::npos,
         "private functions are excluded");
  expect(html.find("href=\"#sample-widget\"") != std::string::npos,
         "known documentation links are resolved");
  expect(html.find("&lt;unsafe&gt;") != std::string::npos &&
             html.find("<unsafe>") == std::string::npos,
         "documentation text is HTML-escaped");
  expect(first.unresolved_links.size() == 1 &&
             first.unresolved_links[0].symbol == "Missing",
         "unknown documentation links are reported");

  const std::filesystem::path blocked = root / "blocked";
  {
    std::ofstream blocker{blocked};
    blocker << "not a directory";
  }
  bool rejected_output = false;
  try {
    static_cast<void>(janus::driver::generate_documentation(
        programs, {"sample-package", "1.2.3", blocked}));
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
