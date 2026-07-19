#include "janus/driver/formatter.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

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

std::string format_source(std::string_view source) {
  std::istringstream input{std::string{source}};
  std::ostringstream output;
  std::string line;
  int indentation = 0;
  bool previous_blank = false;
  while (std::getline(input, line)) {
    const std::string_view content = trim(line);
    if (content.empty()) {
      if (!previous_blank)
        output << '\n';
      previous_blank = true;
      continue;
    }
    previous_blank = false;
    const bool starts_with_close = content.front() == '}';
    const int line_indentation =
        std::max(0, indentation - (starts_with_close ? 1 : 0));
    output << std::string(static_cast<std::size_t>(line_indentation) * 4, ' ')
           << content << '\n';
    const auto [opens, closes] = braces(content);
    indentation = std::max(0, indentation + opens - closes);
  }
  return output.str();
}

} // namespace janus::driver
