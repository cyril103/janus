#include "janus/driver/api_index.hpp"
#include "janus/frontend/parser.hpp"

#include <iostream>
#include <algorithm>
#include <stdexcept>
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

void expect_invalid(std::string_view json, std::string_view message) {
  try {
    static_cast<void>(janus::driver::parse_api_index(json));
    expect(false, message);
  } catch (const std::runtime_error &) {
  }
}
} // namespace

int main() {
  janus::frontend::Parser parser{R"(
/// Searchable module.
module sample
/// Performs an atomic write safely.
def write[T](value : T, path : string) : bool { return true }
/// Integer overload.
def write(value : int, path : string) : bool { return true }
/// Old writer.
/// @deprecated use [[write]]
def oldWrite(path : string) : bool { return true }
private def secret() : int { return 1 }
private class HiddenOwner() { internal def hidden() : int { return 2 } }
)"};
  std::vector<janus::ast::Program> programs;
  programs.push_back(parser.parse_program());
  const auto index = janus::driver::build_api_index(
      programs, {"fixture", "1.0.0"});
  expect(index.format_version == 1, "the index format is versioned");
  expect(index.symbols.size() == 3, "only public symbols are indexed");
  janus::frontend::Parser constant_parser{
      "module sample\nconst answer : int = 6 * 7\n"};
  std::vector<janus::ast::Program> constant_programs;
  constant_programs.push_back(constant_parser.parse_program());
  const auto constant_index = janus::driver::build_api_index(
      constant_programs, {"fixture", "1.0.0"});
  const auto constant = constant_index.symbols.begin();
  expect(constant != constant_index.symbols.end() &&
             constant->signature.find("int:u64:000000000000002a") !=
                 std::string::npos &&
             constant->signature.find("origin sample.answer") !=
                 std::string::npos,
         "API constants expose type, canonical value, and origin");
  expect(index.symbols[0].module == "sample" &&
             index.symbols[0].required_import == "sample",
         "module and required import are always exposed");
  expect(index.symbols[0].visibility == "public",
         "visibility is explicit");
  expect(index.symbols[0].documentation_link.starts_with("#"),
         "a stable documentation link is exposed");
  janus::frontend::Parser tailrec_parser{
      "module recursion\ntailrec def loop(value : int) : int { return loop(value) }\n"};
  std::vector<janus::ast::Program> tailrec_programs;
  tailrec_programs.push_back(tailrec_parser.parse_program());
  const auto tailrec_index = janus::driver::build_api_index(
      tailrec_programs, {"fixture", "1.0.0"});
  expect(tailrec_index.symbols.size() == 1 &&
             tailrec_index.symbols[0].signature.starts_with("tailrec def loop"),
         "API signatures preserve the tailrec contract modifier");

  const auto by_name = janus::driver::search_api(index, {"write"});
  expect(by_name.size() >= 2 && by_name[0].symbol->simple_name == "write" &&
             by_name[1].symbol->simple_name == "write" &&
             by_name[0].symbol->signature != by_name[1].symbol->signature,
         "overloads remain distinct");
  expect(by_name[0].symbol->simple_name == "write",
         "simple-name matches rank first");
  const auto by_qualified =
      janus::driver::search_api(index, {"sample.write"});
  expect(by_qualified.size() == 2 && by_qualified[0].score > 0,
         "qualified-name search is supported");
  const auto by_doc =
      janus::driver::search_api(index, {"atomic safely"});
  expect(!by_doc.empty() && by_doc[0].symbol->simple_name == "write",
         "documentation terms are searchable");
  const auto filtered = janus::driver::search_api(
      index, {"write", "sample", "function", "fixture"});
  expect(filtered.size() >= 2 && filtered[0].symbol->simple_name == "write" &&
             filtered[1].symbol->simple_name == "write",
         "module, kind, and package filters compose");
  const auto fuzzy = janus::driver::search_api(index, {"wirte"});
  expect(fuzzy.size() == 2 && fuzzy[0].symbol->simple_name == "write",
         "lexically close misspellings remain discoverable");
  const auto generic_preferred = janus::driver::search_api(
      index, {"write", {}, {}, {}, std::nullopt, 2, {"sample"}, 1});
  expect(generic_preferred.size() >= 2 &&
             generic_preferred[0].symbol->generic_parameters.size() == 1,
         "signature genericity participates in ranking");

  auto imported_peer = index;
  for (auto &symbol : imported_peer.symbols) {
    symbol.package = "zz-peer";
    symbol.module = "zz.sample";
    symbol.required_import = "zz.sample";
    symbol.qualified_name.replace(0, std::string{"sample"}.size(), "zz.sample");
  }
  imported_peer.package = "zz-peer";
  const auto merged_for_import_ranking =
      janus::driver::merge_api_indexes({index, imported_peer});
  const auto imported_preferred = janus::driver::search_api(
      merged_for_import_ranking,
      {"write", {}, {}, {}, std::nullopt, std::nullopt, {"zz.sample"}});
  expect(!imported_preferred.empty() &&
             imported_preferred[0].symbol->required_import == "zz.sample",
         "already imported modules participate in ranking");

  const std::string first = janus::driver::serialize_api_index(index);
  const std::string second = janus::driver::serialize_api_index(index);
  expect(first == second, "JSON output is deterministic");
  expect(first.find("\"format_version\":1") != std::string::npos &&
             first.find("\"generic_constraints\"") != std::string::npos &&
             first.find("\"replacement\":\"sample.write\"") !=
                 std::string::npos,
         "JSON contains the versioned discovery contract");
  const auto restored = janus::driver::parse_api_index(first);
  expect(restored.symbols.size() == index.symbols.size(),
         "offline indexes can be loaded without source traversal");
  expect(restored.package_version == "1.0.0" &&
             restored.symbols[0].kind == index.symbols[0].kind &&
             restored.symbols[0].generic_parameters ==
                 index.symbols[0].generic_parameters &&
             restored.symbols[0].deprecated == index.symbols[0].deprecated,
         "round trips preserve package, kinds, generics, and deprecation");

  expect_invalid(R"({"format_version":1,"package":"p","package_version":"1"})",
                 "symbols is required");
  expect_invalid(
      R"({"format_version":"1","package":"p","package_version":"1","symbols":[]})",
      "format_version has a strict integer type");
  expect_invalid(
      R"({"format_version":1,"package":"p","package_version":"1","symbols":[7]})",
      "every symbol must be an object");
  expect_invalid(
      R"({"format_version":1,"package":"p","package_version":"1","symbols":[{"simple_name":"f"}]})",
      "required symbol fields may not be omitted");
  std::string malformed_parameter = first;
  const std::size_t parameters = malformed_parameter.find("\"parameters\":[");
  malformed_parameter.insert(parameters + std::string_view{"\"parameters\":["}.size(),
                             "false,");
  expect_invalid(malformed_parameter,
                 "every parameter must be a well-formed object");

  janus::frontend::Parser rich_parser{R"(
module rich
trait Constraint[T] {}
trait GenericTrait[T <: Constraint[T]] {}
enum GenericEnum[T] { Empty, Value(T) }
class GenericClass[T <: Constraint[T]](input : T, borrow val saved : T) {}
extern def ownedVariadic[T](consume value : T, borrow other : T, ...) : owned T
)"};
  std::vector<janus::ast::Program> rich_programs;
  rich_programs.push_back(rich_parser.parse_program());
  const auto rich = janus::driver::build_api_index(
      rich_programs, {"rich-package", "2.0.0"});
  const auto find_rich = [&](std::string_view qualified, std::string_view kind) {
    return std::find_if(rich.symbols.begin(), rich.symbols.end(),
                        [&](const auto &symbol) {
                          return symbol.qualified_name == qualified &&
                                 symbol.kind == kind;
                        });
  };
  const auto generic_class = find_rich("rich.GenericClass", "class");
  expect(generic_class != rich.symbols.end() &&
             generic_class->signature ==
                 "class GenericClass[T](input : T, borrow val saved : T)" &&
             generic_class->generic_parameters ==
                 std::vector<std::string>{"T"} &&
             generic_class->generic_constraints ==
                 std::vector<std::string>{"T : Constraint[T]"} &&
             generic_class->parameters.size() == 2,
         "generic classes preserve constructor signatures and metadata");
  const auto generic_trait = find_rich("rich.GenericTrait", "trait");
  const auto generic_enum = find_rich("rich.GenericEnum", "enum");
  const auto payload = find_rich("rich.GenericEnum.Value", "variant");
  expect(generic_trait != rich.symbols.end() &&
             generic_trait->signature == "trait GenericTrait[T]" &&
             !generic_trait->generic_constraints.empty() &&
             generic_enum != rich.symbols.end() &&
             generic_enum->signature == "enum GenericEnum[T]" &&
             payload != rich.symbols.end() && payload->signature == "Value(T)",
         "generic traits, enums, and variant payloads use documentation signatures");
  const auto variadic = find_rich("rich.ownedVariadic", "function");
  expect(variadic != rich.symbols.end() &&
             variadic->signature ==
                 "def ownedVariadic[T](consume value : T, borrow other : T, ...) : owned T",
         "function ownership and variadic markers are preserved");

  std::string wrong_package = first;
  const std::size_t symbol_package = wrong_package.find("\"package\":\"fixture\"", 30);
  wrong_package.replace(symbol_package, std::string{"\"package\":\"fixture\""}.size(),
                        "\"package\":\"other\"");
  expect_invalid(wrong_package,
                 "symbol packages must agree with the containing index");

  auto conflicting = index;
  auto duplicate = conflicting.symbols.front();
  duplicate.summary += " conflict";
  conflicting.symbols.push_back(std::move(duplicate));
  expect_invalid(janus::driver::serialize_api_index(conflicting),
                 "conflicting duplicate identities are rejected while parsing");
  try {
    static_cast<void>(janus::driver::merge_api_indexes({index, conflicting}));
    expect(false, "conflicting duplicate identities are rejected while merging");
  } catch (const std::runtime_error &) {
  }

  auto different_kind = index;
  different_kind.symbols.front().kind = "method";
  const auto kind_aware = janus::driver::merge_api_indexes({index, different_kind});
  expect(kind_aware.symbols.size() == index.symbols.size() + 1,
         "merge identity includes symbol kind");

  const auto installed = janus::driver::load_api_index(JANUS_INSTALLED_API_INDEX);
  expect(installed.format_version == janus::driver::api_index_format_version &&
             !installed.package_version.empty() && !installed.symbols.empty(),
         "the installed standard-library artifact uses the shared schema");
  std::size_t stdlib_types_with_generic_syntax = 0;
  bool stdlib_generic_metadata_complete = true;
  bool slice_metadata_complete = false;
  bool mutable_slice_metadata_complete = false;
  for (const auto &symbol : installed.symbols) {
    const bool type = symbol.kind == "class" || symbol.kind == "struct" ||
                      symbol.kind == "trait" || symbol.kind == "enum";
    if (!type || symbol.signature.find('[') == std::string::npos)
      continue;
    ++stdlib_types_with_generic_syntax;
    const std::size_t name = symbol.signature.find(symbol.simple_name);
    const bool declaration_is_generic =
        name != std::string::npos &&
        name + symbol.simple_name.size() < symbol.signature.size() &&
        symbol.signature[name + symbol.simple_name.size()] == '[';
    if (declaration_is_generic && symbol.generic_parameters.empty())
      stdlib_generic_metadata_complete = false;
    if (symbol.qualified_name == "std.slice.Slice")
      slice_metadata_complete =
          symbol.generic_parameters == std::vector<std::string>{"T"};
    if (symbol.qualified_name == "std.slice.MutableSlice")
      mutable_slice_metadata_complete =
          symbol.generic_parameters == std::vector<std::string>{"T"};
  }
  expect(stdlib_types_with_generic_syntax == 33 &&
             stdlib_generic_metadata_complete && slice_metadata_complete &&
             mutable_slice_metadata_complete,
         "all 33 stdlib type signatures preserve generic declaration metadata");
  expect(janus::driver::format_api_search(by_doc, "human") ==
             janus::driver::format_api_search(by_doc, "human"),
         "human output is deterministic");
  return failures == 0 ? 0 : 1;
}
