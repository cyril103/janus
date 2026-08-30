#include "janus/driver/formatter.hpp"

#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"

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

template <typename Callback>
void for_each_function(const janus::ast::Program &program, Callback callback) {
  for (const janus::ast::FunctionDeclaration &function : program.functions)
    callback(function);
  for (const janus::ast::ClassDeclaration &declaration : program.classes)
    for (const janus::ast::FunctionDeclaration &method : declaration.methods)
      callback(method);
  for (const janus::ast::ExtensionDeclaration &declaration : program.extensions)
    for (const janus::ast::FunctionDeclaration &method : declaration.methods)
      callback(method);
  for (const janus::ast::TraitDeclaration &declaration : program.traits)
    for (const janus::ast::FunctionDeclaration &method : declaration.methods)
      callback(method);
}

std::string canonicalize_function_arrows(std::string_view source) {
  std::vector<std::size_t> arrows;
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    for_each_function(program, [&](const janus::ast::FunctionDeclaration &fn) {
      if (fn.expression_body_arrow.has_value())
        arrows.push_back(fn.expression_body_arrow->offset);
    });
  } catch (const janus::CompileError &) {
    return std::string{source};
  }
  std::sort(arrows.rbegin(), arrows.rend());
  std::string result{source};
  for (const std::size_t arrow : arrows) {
    std::size_t begin = arrow;
    while (begin > 0 &&
           std::isspace(static_cast<unsigned char>(result[begin - 1])))
      --begin;
    if (result.substr(begin, arrow - begin).find('\n') != std::string::npos) {
      const std::size_t preceding_line_start =
          begin == 0 ? 0 : result.rfind('\n', begin - 1) + 1;
      const std::string_view preceding_line{
          result.data() + preceding_line_start, begin - preceding_line_start};
      if (preceding_line.find("//") != std::string_view::npos)
        begin = result.rfind('\n', arrow - 1) + 1;
    }
    std::size_t end = arrow + 2;
    while (end < result.size() && result[end] != '\n' &&
           std::isspace(static_cast<unsigned char>(result[end])))
      ++end;
    const bool expression_same_line = end < result.size() && result[end] != '\n';
    result.replace(begin, end - begin,
                   expression_same_line ? " => " : " =>");
  }
  return result;
}

struct ExpressionLineRange {
  std::size_t arrow_line{};
  std::size_t start_line{};
  std::size_t end_line{};
  std::size_t start_offset{};
  std::size_t arrow_offset{};
  std::size_t end_offset{};
  int arrow_block_depth{};
  bool indent_continuation{true};
};

std::vector<ExpressionLineRange>
expression_line_ranges(std::string_view source) {
  std::vector<std::size_t> line_starts{0};
  for (std::size_t offset = 0; offset < source.size(); ++offset)
    if (source[offset] == '\n')
      line_starts.push_back(offset + 1);
  const auto line_at = [&](std::size_t offset) {
    return static_cast<std::size_t>(
        std::upper_bound(line_starts.begin(), line_starts.end(), offset) -
        line_starts.begin() - 1);
  };
  std::vector<ExpressionLineRange> ranges;
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    for_each_function(program, [&](const janus::ast::FunctionDeclaration &fn) {
      if (fn.expression_body_arrow.has_value() &&
          fn.expression_body_start.has_value() && fn.expression_body_end > 0)
        ranges.push_back(ExpressionLineRange{
            line_at(fn.expression_body_arrow->offset),
            line_at(fn.expression_body_start->offset),
            line_at(fn.expression_body_end - 1),
            fn.expression_body_start->offset,
            fn.expression_body_arrow->offset,
            fn.expression_body_end});
    });
  } catch (const janus::CompileError &) {
  }
  for (ExpressionLineRange &range : ranges) {
    range.indent_continuation =
        range.start_line > range.arrow_line ||
        !source.substr(range.start_offset).starts_with("match ");
  }
  int block_depth = 0;
  std::istringstream lines{std::string{source}};
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(lines, line)) {
    const std::string_view content = trim(line);
    const DelimiterCounts leading = leading_closes(content);
    const int visible = std::max(0, block_depth - leading.block_closes);
    for (ExpressionLineRange &range : ranges)
      if (range.arrow_line == line_number)
        range.arrow_block_depth = visible;
    const DelimiterCounts counts = delimiters(content);
    block_depth = std::max(
        0, block_depth + counts.block_opens - counts.block_closes);
    ++line_number;
  }
  return ranges;
}

bool safely_flattenable(std::string_view expression) {
  // Newlines are ordinary whitespace in expressions, but flattening a line
  // comment would make the following source part of that comment.
  return expression.find("//") == std::string_view::npos;
}

std::string flatten_expression(std::string_view expression) {
  std::istringstream lines{std::string{expression}};
  std::string line;
  std::string result;
  while (std::getline(lines, line)) {
    const std::string_view content = trim(line);
    if (content.empty())
      continue;
    if (!result.empty())
      result.push_back(' ');
    result.append(content);
  }
  return result;
}

std::string canonicalize_expression_layout(std::string_view source,
                                           const janus::driver::FormatOptions &options) {
  std::vector<ExpressionLineRange> ranges = expression_line_ranges(source);
  std::sort(ranges.begin(), ranges.end(),
            [](const auto &left, const auto &right) {
              return left.arrow_offset > right.arrow_offset;
            });
  std::string result{source};
  for (const ExpressionLineRange &range : ranges) {
    const std::string_view arrow_trivia = source.substr(
        range.arrow_offset + 2,
        range.start_offset - (range.arrow_offset + 2));
    if (std::any_of(arrow_trivia.begin(), arrow_trivia.end(), [](char character) {
          return !std::isspace(static_cast<unsigned char>(character));
        }))
      continue;
    const std::string_view expression =
        source.substr(range.start_offset, range.end_offset - range.start_offset);
    if (!safely_flattenable(expression))
      continue;
    const std::string flat = flatten_expression(expression);
    const std::size_t line_start = source.rfind('\n', range.arrow_offset);
    const std::size_t prefix_start =
        line_start == std::string_view::npos ? 0 : line_start + 1;
    const std::size_t header_length =
        trim(source.substr(prefix_start,
                           range.arrow_offset + 2 - prefix_start)).size();
    const std::size_t complete_length =
        static_cast<std::size_t>(range.arrow_block_depth) * options.indent_width +
        header_length + 1 + flat.size();
    const bool fits = complete_length <= options.max_line_length;
    const std::string replacement =
        fits ? " " + flat
             : "\n" + std::string(
                           static_cast<std::size_t>(range.arrow_block_depth + 1) *
                               options.indent_width,
                           ' ') +
                   flat;
    result.replace(range.arrow_offset + 2,
                   range.end_offset - (range.arrow_offset + 2), replacement);
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
    } else if (key == "max_line_length") {
      if (parsed < 40 || parsed > 400)
        throw std::runtime_error{
            "max_line_length must be between 40 and 400"};
      options.max_line_length = parsed;
    } else {
      throw std::runtime_error{"unknown formatter option '" + key + "'"};
    }
  }
  return options;
}

std::string format_source(std::string_view source,
                          const FormatOptions &options) {
  const std::string arrow_normalized = canonicalize_function_arrows(source);
  const std::string layout_normalized =
      canonicalize_expression_layout(arrow_normalized, options);
  const std::vector<ExpressionLineRange> expression_ranges =
      expression_line_ranges(layout_normalized);
  std::istringstream input{layout_normalized};
  std::ostringstream output;
  std::string line;
  int block_indentation = 0;
  int continuation_depth = 0;
  std::size_t line_number = 0;
  std::size_t blank_lines = 0;
  while (std::getline(input, line)) {
    const std::size_t current_line = line_number++;
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
    const bool expression_body_continuation = std::any_of(
        expression_ranges.begin(), expression_ranges.end(),
        [&](const ExpressionLineRange &range) {
          return current_line > range.arrow_line && current_line <= range.end_line &&
                 range.indent_continuation;
        });
    const int line_indentation =
        visible_blocks + (visible_continuations > 0 ? 1 : 0) +
        (expression_body_continuation ? 1 : 0);
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
