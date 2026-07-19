#include "janus/driver/manifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
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
      if (section != "package")
        throw std::runtime_error{"janus.toml:" + std::to_string(line_number) +
                                 ": unknown section '" + section + "'"};
      continue;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos)
      throw std::runtime_error{"janus.toml:" + std::to_string(line_number) +
                               ": expected key = value"};
    if (section != "package")
      throw std::runtime_error{"janus.toml:" + std::to_string(line_number) +
                               ": key outside [package]"};
    const std::string key = trim(line.substr(0, equals));
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

} // namespace janus::driver
