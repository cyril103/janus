#include "janus/driver/manifest.hpp"
#include "janus/driver/semver.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string trim(std::string value) {
  const auto whitespace = [](unsigned char character) {
    return std::isspace(character) != 0;
  };
  value.erase(value.begin(),
              std::find_if_not(value.begin(), value.end(), whitespace));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(),
              value.end());
  return value;
}

std::string without_comment(std::string_view line) {
  bool quoted = false;
  bool escaped = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char character = line[index];
    if (quoted && character == '\\' && !escaped) {
      escaped = true;
      continue;
    }
    if (character == '"' && !escaped)
      quoted = !quoted;
    if (character == '#' && !quoted)
      return std::string{line.substr(0, index)};
    escaped = false;
  }
  return std::string{line};
}

std::string parse_string(std::string value, std::size_t line) {
  value = trim(std::move(value));
  if (value.size() < 2 || value.front() != '"' || value.back() != '"')
    throw std::runtime_error{"janus.toml:" + std::to_string(line) +
                             ": expected a quoted string"};
  std::string result;
  for (std::size_t index = 1; index + 1 < value.size(); ++index) {
    char character = value[index];
    if (character == '\\') {
      if (++index + 1 >= value.size())
        throw std::runtime_error{"janus.toml:" + std::to_string(line) +
                                 ": invalid string escape"};
      character = value[index];
      if (character != '\\' && character != '"')
        throw std::runtime_error{"janus.toml:" + std::to_string(line) +
                                 ": unsupported string escape"};
    }
    result += character;
  }
  return result;
}

std::map<std::string, std::string> parse_inline_table(std::string value,
                                                      std::size_t line) {
  value = trim(std::move(value));
  if (value.size() < 2 || value.front() != '{' || value.back() != '}')
    throw std::runtime_error{"janus.toml:" + std::to_string(line) +
                             ": expected an inline table"};
  value = trim(value.substr(1, value.size() - 2));
  std::map<std::string, std::string> fields;
  while (!value.empty()) {
    const std::size_t equals = value.find('=');
    if (equals == std::string::npos)
      throw std::runtime_error{"janus.toml:" + std::to_string(line) +
                               ": invalid dependency field"};
    const std::string key = trim(value.substr(0, equals));
    value = trim(value.substr(equals + 1));
    if (value.empty() || value.front() != '"')
      throw std::runtime_error{"janus.toml:" + std::to_string(line) +
                               ": dependency values must be strings"};
    std::size_t end = 1;
    while (end < value.size() && value[end] != '"')
      ++end;
    if (end == value.size())
      throw std::runtime_error{"janus.toml:" + std::to_string(line) +
                               ": unterminated dependency value"};
    if (!fields.emplace(key, value.substr(1, end - 1)).second)
      throw std::runtime_error{"janus.toml:" + std::to_string(line) +
                               ": duplicate dependency field '" + key + "'"};
    value = trim(value.substr(end + 1));
    if (value.empty())
      break;
    if (value.front() != ',')
      throw std::runtime_error{"janus.toml:" + std::to_string(line) +
                               ": expected ',' in dependency"};
    value = trim(value.substr(1));
  }
  return fields;
}

void validate(const janus::driver::Manifest &manifest) {
  if (!std::regex_match(manifest.name, std::regex{"[A-Za-z][A-Za-z0-9_-]*"}))
    throw std::runtime_error{
        "janus.toml: package.name must be a valid identifier"};
  if (!std::regex_match(
          manifest.version,
          std::regex{R"([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)"}))
    throw std::runtime_error{
        "janus.toml: package.version must use semantic versioning"};
  if (manifest.entry.empty() || manifest.entry.is_absolute() ||
      manifest.entry.extension() != ".janus" ||
      (!manifest.entry.empty() && *manifest.entry.begin() == ".."))
    throw std::runtime_error{
        "janus.toml: package.entry must be a relative .janus path"};
  for (const janus::driver::Dependency &dependency : manifest.dependencies) {
    if (!std::regex_match(dependency.name,
                          std::regex{"[A-Za-z][A-Za-z0-9_-]*"}))
      throw std::runtime_error{"janus.toml: invalid dependency name '" +
                               dependency.name + "'"};
    if (dependency.is_registry()) {
      if (dependency.version_requirement.empty() ||
          !dependency.revision.empty())
        throw std::runtime_error{"janus.toml: registry dependency '" +
                                 dependency.name + "' requires a version"};
    } else if (dependency.is_git()) {
      if (!dependency.path.empty() ||
          !std::regex_match(dependency.revision, std::regex{"[0-9a-fA-F]{40}"}))
        throw std::runtime_error{
            "janus.toml: Git dependency '" + dependency.name +
            "' requires git and a full 40-character commit rev"};
    } else if (dependency.path.empty() || dependency.path.is_absolute() ||
               !dependency.revision.empty()) {
      throw std::runtime_error{"janus.toml: path dependency '" +
                               dependency.name +
                               "' requires one relative path"};
    }
    try {
      static_cast<void>(janus::driver::matches_version(
          dependency.version_requirement,
          janus::driver::parse_semantic_version("0.0.0")));
    } catch (const std::runtime_error &error) {
      throw std::runtime_error{
          "janus.toml: dependency '" + dependency.name +
          "' has invalid version requirement: " + error.what()};
    }
  }
}

} // namespace

namespace janus::driver {

Manifest load_manifest(const std::filesystem::path &path) {
  std::ifstream input{path};
  if (!input)
    throw std::runtime_error{"cannot open manifest '" + path.string() + "'"};

  Manifest manifest{};
  manifest.path = std::filesystem::absolute(path).lexically_normal();
  std::string section;
  bool has_name = false;
  bool has_version = false;
  bool has_entry = false;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    line = trim(without_comment(line));
    if (line.empty())
      continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(line.substr(1, line.size() - 2));
      if (section != "package" && section != "dependencies")
        throw std::runtime_error{"janus.toml:" + std::to_string(line_number) +
                                 ": unknown section '" + section + "'"};
      continue;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos)
      throw std::runtime_error{"janus.toml:" + std::to_string(line_number) +
                               ": expected key = value"};
    if (section.empty())
      throw std::runtime_error{"janus.toml:" + std::to_string(line_number) +
                               ": key outside a section"};
    const std::string key = trim(line.substr(0, equals));
    if (section == "dependencies") {
      if (std::any_of(manifest.dependencies.begin(),
                      manifest.dependencies.end(),
                      [&key](const Dependency &dependency) {
                        return dependency.name == key;
                      }))
        throw std::runtime_error{"janus.toml:" + std::to_string(line_number) +
                                 ": duplicate dependency '" + key + "'"};
      Dependency dependency;
      dependency.name = key;
      const std::string dependency_value = trim(line.substr(equals + 1));
      if (!dependency_value.empty() && dependency_value.front() == '"') {
        dependency.version_requirement =
            parse_string(dependency_value, line_number);
      } else {
        const std::map<std::string, std::string> fields =
            parse_inline_table(dependency_value, line_number);
        if (const auto found = fields.find("path"); found != fields.end())
          dependency.path = found->second;
        if (const auto found = fields.find("git"); found != fields.end())
          dependency.git = found->second;
        if (const auto found = fields.find("rev"); found != fields.end())
          dependency.revision = found->second;
        if (const auto found = fields.find("version"); found != fields.end())
          dependency.version_requirement = found->second;
        for (const auto &[field, unused] : fields) {
          static_cast<void>(unused);
          if (field != "path" && field != "git" && field != "rev" &&
              field != "version")
            throw std::runtime_error{
                "janus.toml:" + std::to_string(line_number) +
                ": unknown dependency field '" + field + "'"};
        }
      }
      manifest.dependencies.push_back(std::move(dependency));
      continue;
    }
    const std::string value =
        parse_string(line.substr(equals + 1), line_number);
    bool *present = nullptr;
    std::string *destination = nullptr;
    if (key == "name") {
      present = &has_name;
      destination = &manifest.name;
    } else if (key == "version") {
      present = &has_version;
      destination = &manifest.version;
    } else if (key == "entry") {
      present = &has_entry;
    } else {
      throw std::runtime_error{"janus.toml:" + std::to_string(line_number) +
                               ": unknown package key '" + key + "'"};
    }
    if (*present)
      throw std::runtime_error{"janus.toml:" + std::to_string(line_number) +
                               ": duplicate key '" + key + "'"};
    *present = true;
    if (destination != nullptr)
      *destination = value;
    else
      manifest.entry = value;
  }
  if (!has_name || !has_version || !has_entry)
    throw std::runtime_error{
        "janus.toml: [package] requires name, version and entry"};
  validate(manifest);
  return manifest;
}

std::filesystem::path find_manifest(const std::filesystem::path &start) {
  std::filesystem::path directory =
      std::filesystem::absolute(start).lexically_normal();
  if (!std::filesystem::is_directory(directory))
    directory = directory.parent_path();
  while (true) {
    const std::filesystem::path candidate = directory / "janus.toml";
    if (std::filesystem::is_regular_file(candidate))
      return candidate;
    const std::filesystem::path parent = directory.parent_path();
    if (parent == directory)
      break;
    directory = parent;
  }
  throw std::runtime_error{
      "could not find janus.toml in this directory or its parents"};
}

} // namespace janus::driver
