#include "janus/driver/doctest.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {

struct FenceDirectives {
  bool executable{};
  bool incomplete{};
  std::string name;
  std::string expected_diagnostic;
};

[[nodiscard]] bool starts_with_fence(std::string_view line) {
  return line.starts_with("```");
}

[[nodiscard]] std::string location(const std::filesystem::path &document,
                                   std::size_t line) {
  return document.generic_string() + ":" + std::to_string(line);
}

[[nodiscard]] FenceDirectives
parse_directives(const std::filesystem::path &document, std::size_t line,
                 std::string_view information) {
  FenceDirectives directives;
  std::istringstream input{std::string{information}};
  std::string token;
  input >> token;
  if (token != "janus")
    return directives;
  while (input >> token) {
    if (token == "doctest") {
      directives.executable = true;
    } else if (token == "incomplete" || token == "ignore") {
      directives.incomplete = true;
    } else if (token.starts_with("compile_fail=")) {
      directives.executable = true;
      directives.expected_diagnostic =
          token.substr(std::string{"compile_fail="}.size());
      if (!std::regex_match(directives.expected_diagnostic,
                            std::regex{R"(J(?:[A-Z]{3})?[0-9]{4})"}))
        throw std::runtime_error{
            location(document, line) +
            ": compile_fail requires a diagnostic code such as JANA0001"};
    } else if (token.starts_with("name=")) {
      directives.name = token.substr(std::string{"name="}.size());
      if (!std::regex_match(directives.name, std::regex{R"([A-Za-z0-9_.-]+)"}))
        throw std::runtime_error{location(document, line) +
                                 ": invalid doctest name"};
    }
  }
  if (directives.executable && directives.incomplete)
    throw std::runtime_error{
        location(document, line) +
        ": a Janus block cannot be both executable and incomplete"};
  return directives;
}

[[nodiscard]] bool is_within(const std::filesystem::path &path,
                             const std::filesystem::path &root) {
  const std::filesystem::path relative = path.lexically_relative(root);
  return !relative.empty() && !relative.is_absolute() &&
         *relative.begin() != "..";
}

} // namespace

namespace janus::driver {

std::string Doctest::display_name() const {
  std::string display = document.generic_string() + ":" + std::to_string(line);
  if (!name.empty())
    display += " (" + name + ")";
  return display;
}

std::vector<Doctest> parse_doctests(const std::filesystem::path &document,
                                    std::string_view markdown) {
  std::vector<Doctest> tests;
  std::istringstream input{std::string{markdown}};
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (!line.starts_with("```janus"))
      continue;
    const std::size_t fence_line = line_number;
    const FenceDirectives directives =
        parse_directives(document, fence_line, line.substr(3));
    std::string source;
    bool closed = false;
    while (std::getline(input, line)) {
      ++line_number;
      if (starts_with_fence(line)) {
        closed = true;
        break;
      }
      source += line;
      source += '\n';
    }
    if (!closed)
      throw std::runtime_error{location(document, fence_line) +
                               ": unterminated Janus code block"};
    if (!directives.executable)
      continue;
    tests.push_back({document, fence_line + 1, directives.name,
                     std::move(source),
                     directives.expected_diagnostic.empty()
                         ? DoctestExpectation::CompilePass
                         : DoctestExpectation::CompileFail,
                     directives.expected_diagnostic});
  }
  return tests;
}

std::vector<Doctest>
discover_doctests(const std::filesystem::path &package_root,
                  const std::vector<std::filesystem::path> &documentation) {
  const std::filesystem::path root =
      std::filesystem::absolute(package_root).lexically_normal();
  std::set<std::filesystem::path> markdown_files;
  for (const std::filesystem::path &requested : documentation) {
    if (requested.empty() || requested.is_absolute())
      throw std::runtime_error{
          "documentation paths must be relative to the package"};
    const std::filesystem::path path = (root / requested).lexically_normal();
    if (!is_within(path, root))
      throw std::runtime_error{
          "documentation paths must stay within the package"};
    if (!std::filesystem::exists(path))
      continue;
    if (std::filesystem::is_regular_file(path)) {
      if (path.extension() != ".md")
        throw std::runtime_error{"documentation file must use .md: " +
                                 requested.generic_string()};
      markdown_files.insert(path);
      continue;
    }
    if (!std::filesystem::is_directory(path))
      continue;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator{path}) {
      if (entry.is_regular_file() && entry.path().extension() == ".md")
        markdown_files.insert(entry.path().lexically_normal());
    }
  }

  std::vector<Doctest> tests;
  for (const std::filesystem::path &path : markdown_files) {
    std::ifstream input{path, std::ios::binary};
    if (!input)
      throw std::runtime_error{"cannot read documentation file '" +
                               path.string() + "'"};
    const std::string markdown{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    const std::filesystem::path document = path.lexically_relative(root);
    std::vector<Doctest> parsed = parse_doctests(document, markdown);
    tests.insert(tests.end(), std::make_move_iterator(parsed.begin()),
                 std::make_move_iterator(parsed.end()));
  }
  return tests;
}

bool matches_doctest_filter(const Doctest &test, std::string_view filter) {
  return filter.empty() ||
         test.display_name().find(filter) != std::string::npos;
}

} // namespace janus::driver
