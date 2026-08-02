#include "janus/diagnostics/renderer.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {

std::string_view severity_name(janus::DiagnosticSeverity severity) {
  switch (severity) {
  case janus::DiagnosticSeverity::Note:
    return "note";
  case janus::DiagnosticSeverity::Warning:
    return "warning";
  case janus::DiagnosticSeverity::Error:
    return "error";
  }
  return "error";
}

std::string source_line(std::string_view source, std::uint32_t line) {
  if (line == 0)
    return {};
  std::size_t start = 0;
  for (std::uint32_t current = 1; current < line; ++current) {
    start = source.find('\n', start);
    if (start == std::string_view::npos)
      return {};
    ++start;
  }
  const std::size_t end = source.find('\n', start);
  std::string result{source.substr(start, end == std::string_view::npos
                                              ? source.size() - start
                                              : end - start)};
  if (!result.empty() && result.back() == '\r')
    result.pop_back();
  return result;
}

std::size_t digits(std::uint32_t value) {
  std::size_t count = 1;
  while (value >= 10) {
    value /= 10;
    ++count;
  }
  return count;
}

std::string json_string(std::string_view value) {
  std::ostringstream output;
  output << '"';
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (character < 0x20)
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<unsigned>(character) << std::dec;
      else
        output << character;
    }
  }
  output << '"';
  return output.str();
}

void render_location_json(std::ostringstream &output,
                          const janus::SourceLocation &location) {
  output << "{\"offset\":" << location.offset << ",\"line\":" << location.line
         << ",\"column\":" << location.column << '}';
}

std::string render_json(const std::filesystem::path &path,
                        std::span<const janus::Diagnostic> diagnostics) {
  std::ostringstream output;
  output << "{\"schemaVersion\":\"0.5.2\",\"diagnostics\":[";
  bool first_diagnostic = true;
  for (const janus::Diagnostic &diagnostic : diagnostics) {
    if (!first_diagnostic)
      output << ',';
    first_diagnostic = false;
    output << "{\"severity\":"
           << json_string(severity_name(diagnostic.severity)) << ",\"code\":"
           << json_string(janus::diagnostic_code_name(diagnostic.code))
           << ",\"message\":" << json_string(diagnostic.message)
           << ",\"primaryLocation\":{\"file\":"
           << json_string((diagnostic.source_path.empty()
                               ? path
                               : diagnostic.source_path)
                              .generic_string())
           << ",\"position\":";
    render_location_json(output, diagnostic.primary_location);
    output << "},\"notes\":[";
    bool first = true;
    for (const std::string &note : diagnostic.notes) {
      if (!first)
        output << ',';
      first = false;
      output << json_string(note);
    }
    output << "],\"secondaryLocations\":[";
    first = true;
    for (const janus::DiagnosticLocation &secondary :
         diagnostic.secondary_locations) {
      if (!first)
        output << ',';
      first = false;
      output << "{\"label\":" << json_string(secondary.label)
             << ",\"position\":";
      render_location_json(output, secondary.location);
      output << '}';
    }
    output << "],\"suggestions\":[";
    first = true;
    for (const janus::DiagnosticSuggestion &suggestion :
         diagnostic.suggestions) {
      if (!first)
        output << ',';
      first = false;
      output << "{\"message\":" << json_string(suggestion.message)
             << ",\"replacement\":" << json_string(suggestion.replacement)
             << ",\"range\":{\"start\":";
      render_location_json(output, suggestion.range.start);
      output << ",\"end\":";
      render_location_json(output, suggestion.range.end);
      output << "}}";
    }
    output << "]}";
  }
  output << "]}\n";
  return output.str();
}

std::string render_human(const std::filesystem::path &path,
                         std::string_view source,
                         std::span<const janus::Diagnostic> diagnostics,
                         janus::diagnostics::RenderOptions options) {
  std::ostringstream output;
  for (const janus::Diagnostic &diagnostic : diagnostics) {
    const janus::SourceLocation location = diagnostic.primary_location;
    output << path.string() << ':' << location.line << ':' << location.column
           << ": " << severity_name(diagnostic.severity) << ':';
    if (diagnostic.code != janus::DiagnosticCode::Unclassified)
      output << " [" << janus::diagnostic_code_name(diagnostic.code) << ']';
    output << ' ' << diagnostic.message << '\n';

    const std::string full_line = source_line(source, location.line);
    if (!full_line.empty()) {
      const std::size_t line_digits = digits(location.line);
      const std::size_t prefix_width = line_digits + 3;
      const std::size_t available =
          std::max<std::size_t>(20, options.width > prefix_width + 2
                                        ? options.width - prefix_width - 2
                                        : 20);
      std::size_t start = 0;
      if (full_line.size() > available && location.column > available / 2)
        start =
            std::min(full_line.size() - available,
                     static_cast<std::size_t>(location.column) - available / 2);
      std::string excerpt = full_line.substr(start, available);
      if (start > 0) {
        excerpt.replace(0, std::min<std::size_t>(3, excerpt.size()), "...");
      }
      if (start + available < full_line.size() && excerpt.size() >= 3)
        excerpt.replace(excerpt.size() - 3, 3, "...");

      output << std::string(line_digits, ' ') << " |\n";
      output << location.line << " | " << excerpt << '\n';
      const std::size_t caret =
          location.column > start ? location.column - start - 1 : 0;
      output << std::string(line_digits, ' ') << " | "
             << std::string(caret, ' ') << '^' << '\n';
    }
    for (const std::string &note : diagnostic.notes)
      output << "  = note: " << note << '\n';
    for (const janus::DiagnosticSuggestion &suggestion : diagnostic.suggestions)
      output << "  = help: " << suggestion.message << '\n';
  }
  return output.str();
}

} // namespace

namespace janus::diagnostics {

std::string render_diagnostics(const std::filesystem::path &path,
                               std::string_view source,
                               std::span<const Diagnostic> diagnostics,
                               DiagnosticFormat format, RenderOptions options) {
  if (format == DiagnosticFormat::Json)
    return render_json(path, diagnostics);
  return render_human(path, source, diagnostics, options);
}

} // namespace janus::diagnostics
