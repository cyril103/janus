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

struct DelimiterCounts {
  int block_opens{0};
  int block_closes{0};
  int continuation_opens{0};
  int continuation_closes{0};
};

DelimiterCounts delimiters(std::string_view line) {
  DelimiterCounts counts;
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
      ++counts.block_opens;
    else if (character == '}')
      ++counts.block_closes;
    else if (character == '[' || character == '(')
      ++counts.continuation_opens;
    else if (character == ']' || character == ')')
      ++counts.continuation_closes;
  }
  return counts;
}

DelimiterCounts leading_closes(std::string_view line) {
  DelimiterCounts counts;
  while (!line.empty() &&
         (line.front() == '}' || line.front() == ']' || line.front() == ')')) {
    if (line.front() == '}')
      ++counts.block_closes;
    else
      ++counts.continuation_closes;
    line.remove_prefix(1);
  }
  return counts;
}

std::string canonicalize_compound_assignment(std::string_view line) {
  std::string result;
  result.reserve(line.size());
  char quote = '\0';
  bool escaped = false;
  for (std::size_t index = 0; index < line.size();) {
    const char character = line[index];
    if (quote != '\0') {
      result.push_back(character);
      ++index;
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
      result.push_back(character);
      ++index;
      continue;
    }
    if (character == '/' && index + 1 < line.size() && line[index + 1] == '/') {
      result.append(line.substr(index));
      break;
    }
    const bool three = index + 2 < line.size() && line[index + 2] == '=' &&
                       ((character == '<' && line[index + 1] == '<') ||
                        (character == '>' && line[index + 1] == '>'));
    const bool two = index + 1 < line.size() && line[index + 1] == '=' &&
                     std::string_view{"+-*/%&|^"}.find(character) !=
                         std::string_view::npos;
    if (!three && !two) {
      result.push_back(character);
      ++index;
      continue;
    }

    while (!result.empty() &&
           std::isspace(static_cast<unsigned char>(result.back())))
      result.pop_back();
    if (!result.empty())
      result.push_back(' ');
    const std::size_t length = three ? 3 : 2;
    result.append(line.substr(index, length));
    index += length;
    while (index < line.size() &&
           std::isspace(static_cast<unsigned char>(line[index])))
      ++index;
    if (index < line.size())
      result.push_back(' ');
  }
  return result;
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
  int block_indentation = 0;
  int continuation_depth = 0;
  std::size_t blank_lines = 0;
  while (std::getline(input, line)) {
    const std::string_view trimmed_content = trim(line);
    const std::string normalized =
        canonicalize_compound_assignment(trimmed_content);
    const std::string_view content = normalized;
    if (content.empty()) {
      if (blank_lines < options.max_blank_lines)
        output << '\n';
      ++blank_lines;
      continue;
    }
    blank_lines = 0;
    const DelimiterCounts leading = leading_closes(content);
    const int visible_blocks =
        std::max(0, block_indentation - leading.block_closes);
    const int visible_continuations =
        std::max(0, continuation_depth - leading.continuation_closes);
    const int line_indentation =
        visible_blocks + (visible_continuations > 0 ? 1 : 0);
    output << std::string(static_cast<std::size_t>(line_indentation) *
                             options.indent_width,
                         ' ')
           << content << '\n';
    const DelimiterCounts counts = delimiters(content);
    block_indentation =
        std::max(0, block_indentation + counts.block_opens -
                        counts.block_closes);
    continuation_depth =
        std::max(0, continuation_depth + counts.continuation_opens -
                        counts.continuation_closes);
  }
  return output.str();
}

} // namespace janus::driver
