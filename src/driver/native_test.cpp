#include "janus/driver/native_test.hpp"

#include "janus/frontend/parser.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace {

std::string_view trim(std::string_view value) {
  const auto whitespace = [](char character) {
    return character == ' ' || character == '\t' || character == '\r';
  };
  while (!value.empty() && whitespace(value.front()))
    value.remove_prefix(1);
  while (!value.empty() && whitespace(value.back()))
    value.remove_suffix(1);
  return value;
}

struct Metadata {
  bool test{};
  bool ignored{};
  bool serial{};
  std::optional<std::string> expected_panic;
};

Metadata metadata(std::string_view documentation) {
  Metadata result;
  while (!documentation.empty()) {
    const std::size_t newline = documentation.find('\n');
    const std::string_view line = trim(documentation.substr(0, newline));
    if (line == "@test")
      result.test = true;
    else if (line == "@ignore" || line == "@ignored")
      result.ignored = true;
    else if (line == "@serial")
      result.serial = true;
    else if (line == "@shouldPanic")
      result.expected_panic = std::string{};
    else if (line.starts_with("@shouldPanic "))
      result.expected_panic = std::string{trim(line.substr(13))};
    if (newline == std::string_view::npos)
      break;
    documentation.remove_prefix(newline + 1);
  }
  return result;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  if (!input)
    throw std::runtime_error{"cannot read test source '" + path.string() + "'"};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

} // namespace

namespace janus::driver {

std::vector<NativeTest>
discover_native_tests(const std::filesystem::path &tests_root) {
  std::vector<std::filesystem::path> sources;
  if (std::filesystem::is_directory(tests_root)) {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(tests_root))
      if (entry.is_regular_file() && entry.path().extension() == ".janus")
        sources.push_back(entry.path());
  }
  std::sort(sources.begin(), sources.end());

  std::vector<NativeTest> tests;
  std::unordered_set<std::string> identifiers;
  for (const std::filesystem::path &source : sources) {
    const std::string contents = read_file(source);
    const ast::Program program = frontend::Parser{contents}.parse_program();
    std::filesystem::path relative =
        std::filesystem::relative(source, tests_root);
    relative.replace_extension();
    const std::string prefix = relative.generic_string();
    bool found = false;
    bool has_main = false;
    for (const ast::FunctionDeclaration &function : program.functions) {
      has_main = has_main || function.name == "main";
      const Metadata attributes = metadata(function.documentation);
      if (!attributes.test)
        continue;
      found = true;
      if (!function.parameters.empty() || !function.type_parameters.empty() ||
          function.return_type.name != "Unit" || function.is_external ||
          function.is_private)
        throw CompileError{
            function.location,
            "test function '" + function.name +
                "' must be public, non-generic, non-external, take no "
                "parameters, and return Unit"};
      const std::string identifier = prefix + "." + function.name;
      if (!identifiers.insert(identifier).second)
        throw CompileError{function.location,
                           "duplicate test identifier '" + identifier + "'"};
      tests.push_back(NativeTest{source, function.name, identifier,
                                 function.location, attributes.ignored,
                                 attributes.serial, attributes.expected_panic,
                                 false});
    }
    if (found && has_main)
      throw CompileError{{1, 1},
                         "a file containing @test functions must not define "
                         "main"};
    if (!found) {
      if (!identifiers.insert(prefix).second)
        throw CompileError{{1, 1},
                           "duplicate test identifier '" + prefix + "'"};
      tests.push_back(NativeTest{
          source, prefix, prefix, {1, 1}, false, true, std::nullopt, true});
    }
  }
  return tests;
}

bool matches_native_test_filter(const NativeTest &test, std::string_view filter,
                                bool exact) {
  if (filter.empty())
    return true;
  return exact ? test.identifier == filter
               : test.identifier.find(filter) != std::string::npos;
}

std::string native_test_source(std::string_view source,
                               const NativeTest &test) {
  if (test.legacy)
    return std::string{source};
  std::string generated{source};
  generated += "\n\ndef main() : int {\n    ";
  generated += test.name;
  generated += "()\n    return 0\n}\n";
  return generated;
}

} // namespace janus::driver
