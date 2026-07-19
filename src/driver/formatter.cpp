#include "janus/driver/formatter.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

std::string_view trim(std::string_view line) {
  while (!line.empty() &&
         std::isspace(static_cast<unsigned char>(line.front())))
    line.remove_prefix(1);
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
    line.remove_suffix(1);
  return line;
}

std::pair<int, int> braces(std::string_view line) {
  int opens = 0;
  int closes = 0;
  char quote = '\0';
  bool escaped = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char character = line[index];
    if (quote != '\0') {
      if (escaped)
        escaped = false;
      else if (character == '\\')
        escaped = true;
      else if (character == quote)
        quote = '\0';
      continue;
    }
    if (character == '"' || character == '\'') {
      quote = character;
      continue;
    }
    if (character == '/' && index + 1 < line.size() && line[index + 1] == '/')
      break;
    if (character == '{')
      ++opens;
    else if (character == '}')
      ++closes;
  }
  return {opens, closes};
}

} // namespace

namespace janus::driver {

FormatOptions load_format_options(const std::filesystem::path &path) {
  FormatOptions options;
  std::ifstream input{path};
  if (!input)
    return options;

  std::string line;
  while (std::getline(input, line)) {
    std::string_view entry = trim(line);
    if (entry.empty() || entry.starts_with('#'))
      continue;
    const std::size_t separator = entry.find('=');
    if (separator == std::string_view::npos)
      throw std::runtime_error{"invalid formatter configuration entry"};
    const std::string key{trim(entry.substr(0, separator))};
    const std::string value{trim(entry.substr(separator + 1))};
    std::size_t parsed = 0;
    try {
      parsed = std::stoul(value);
    } catch (const std::exception &) {
      throw std::runtime_error{"formatter option '" + key +
                               "' requires an integer"};
    }
    if (key == "indent_width") {
      if (parsed == 0 || parsed > 16)
        throw std::runtime_error{"indent_width must be between 1 and 16"};
      options.indent_width = parsed;
    } else if (key == "max_blank_lines") {
      if (parsed > 4)
        throw std::runtime_error{"max_blank_lines must be between 0 and 4"};
      options.max_blank_lines = parsed;
    } else {
      throw std::runtime_error{"unknown formatter option '" + key + "'"};
    }
  }
  return options;
}

std::string format_source(std::string_view source,
                          const FormatOptions &options) {
  std::istringstream input{std::string{source}};
  std::ostringstream output;
  std::string line;
  int indentation = 0;
  std::size_t blank_lines = 0;
  while (std::getline(input, line)) {
    const std::string_view content = trim(line);
    if (content.empty()) {
      if (blank_lines < options.max_blank_lines)
        output << '\n';
      ++blank_lines;
      continue;
    }
    blank_lines = 0;
    const bool starts_with_close = content.front() == '}';
    const int line_indentation =
        std::max(0, indentation - (starts_with_close ? 1 : 0));
    output << std::string(static_cast<std::size_t>(line_indentation) *
                             options.indent_width,
                         ' ')
           << content << '\n';
    const auto [opens, closes] = braces(content);
    indentation = std::max(0, indentation + opens - closes);
  }
  return output.str();
}

} // namespace janus::driver
