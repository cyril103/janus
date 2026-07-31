#include "janus/driver/documentation.hpp"

#include "janus/frontend/parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

struct DocumentedParameter {
  std::string name;
  std::string type;
  std::string description;
};

struct StructuredDocumentation {
  std::string summary;
  std::vector<std::string> details;
  std::map<std::string, std::string> parameter_descriptions;
  std::vector<std::string> duplicate_parameters;
  std::string return_description;
  bool has_return{};
  std::vector<std::string> examples;
};

struct Symbol {
  std::string module;
  std::string name;
  std::string qualified_name;
  std::string kind;
  std::string signature;
  std::string documentation;
  StructuredDocumentation structured;
  std::vector<DocumentedParameter> parameters;
  std::string return_type;
  bool is_function{};
  std::string anchor;
  std::string parent;
};

std::string trim(std::string_view value) {
  const std::size_t first = value.find_first_not_of(" \t\r");
  if (first == std::string_view::npos)
    return {};
  const std::size_t last = value.find_last_not_of(" \t\r");
  return std::string{value.substr(first, last - first + 1)};
}

bool is_unit_or_void(std::string_view type) {
  std::string normalized;
  normalized.reserve(type.size());
  std::transform(type.begin(), type.end(), std::back_inserter(normalized),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return normalized == "unit" || normalized == "void";
}

StructuredDocumentation parse_documentation(std::string_view source) {
  StructuredDocumentation result;
  std::vector<std::string> prose;
  std::string example;
  bool reading_example = false;
  bool inside_example_fence = false;
  std::istringstream lines{std::string{source}};
  std::string line;
  while (std::getline(lines, line)) {
    const std::string stripped = trim(line);
    if (reading_example) {
      if (!inside_example_fence &&
          (stripped.rfind("@param", 0) == 0 ||
           stripped.rfind("@return", 0) == 0 ||
           stripped == "@example")) {
        result.examples.push_back(trim(example));
        example.clear();
        reading_example = false;
      } else {
        if (!example.empty())
          example += '\n';
        example += line;
        if (stripped.rfind("```", 0) == 0)
          inside_example_fence = !inside_example_fence;
        continue;
      }
    }
    if (stripped == "@example") {
      reading_example = true;
      continue;
    }
    if (stripped.rfind("@param", 0) == 0 &&
        (stripped.size() == 6 || stripped[6] == ' ' ||
         stripped[6] == '\t')) {
      const std::string body = trim(std::string_view{stripped}.substr(6));
      const std::size_t separator = body.find_first_of(" \t");
      const std::string name = body.substr(0, separator);
      const std::string description =
          separator == std::string::npos
              ? std::string{}
              : trim(std::string_view{body}.substr(separator + 1));
      if (!name.empty()) {
        const auto [entry, inserted] =
            result.parameter_descriptions.emplace(name, description);
        static_cast<void>(entry);
        if (!inserted)
          result.duplicate_parameters.push_back(name);
      }
      continue;
    }
    if (stripped.rfind("@return", 0) == 0 &&
        (stripped.size() == 7 || stripped[7] == ' ' ||
         stripped[7] == '\t')) {
      result.has_return = true;
      result.return_description =
          trim(std::string_view{stripped}.substr(7));
      continue;
    }
    prose.push_back(line);
  }
  if (reading_example)
    result.examples.push_back(trim(example));
  for (std::string &value : result.examples) {
    if (value.rfind("```", 0) != 0)
      continue;
    const std::size_t first_newline = value.find('\n');
    const std::size_t closing = value.rfind("```");
    if (first_newline != std::string::npos && closing > first_newline)
      value = trim(std::string_view{value}.substr(
          first_newline + 1, closing - first_newline - 1));
  }

  std::vector<std::string> paragraphs;
  std::string paragraph;
  for (const std::string &prose_line : prose) {
    if (trim(prose_line).empty()) {
      if (!paragraph.empty()) {
        paragraphs.push_back(std::move(paragraph));
        paragraph.clear();
      }
    } else {
      if (!paragraph.empty())
        paragraph += '\n';
      paragraph += trim(prose_line);
    }
  }
  if (!paragraph.empty())
    paragraphs.push_back(std::move(paragraph));
  if (!paragraphs.empty()) {
    result.summary = std::move(paragraphs.front());
    result.details.assign(std::make_move_iterator(paragraphs.begin() + 1),
                          std::make_move_iterator(paragraphs.end()));
  }
  return result;
}

std::string html_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
    case '&':
      escaped += "&amp;";
      break;
    case '<':
      escaped += "&lt;";
      break;
    case '>':
      escaped += "&gt;";
      break;
    case '"':
      escaped += "&quot;";
      break;
    case '\'':
      escaped += "&#39;";
      break;
    default:
      escaped += character;
      break;
    }
  }
  return escaped;
}

std::string json_escape(std::string_view value) {
  std::string escaped;
  for (const unsigned char character : value) {
    switch (character) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (character < 0x20) {
        constexpr char digits[] = "0123456789abcdef";
        escaped += "\\u00";
        escaped += digits[(character >> 4) & 0x0F];
        escaped += digits[character & 0x0F];
      } else {
        escaped += static_cast<char>(character);
      }
      break;
    }
  }
  return escaped;
}

std::string anchor_for(std::string_view qualified_name) {
  std::string anchor;
  bool separator = false;
  for (const unsigned char character : qualified_name) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9')) {
      if (separator && !anchor.empty())
        anchor += '-';
      anchor += static_cast<char>(character >= 'A' && character <= 'Z'
                                      ? character - 'A' + 'a'
                                      : character);
      separator = false;
    } else {
      separator = true;
    }
  }
  return anchor.empty() ? "symbol" : anchor;
}

std::string type_name(const janus::ast::TypeReference &type) {
  std::string rendered = type.name;
  if (!type.type_arguments.empty()) {
    rendered += '[';
    for (std::size_t index = 0; index < type.type_arguments.size(); ++index) {
      if (index != 0)
        rendered += ", ";
      rendered += type_name(type.type_arguments[index]);
    }
    rendered += ']';
  }
  return rendered;
}

std::string function_signature(const janus::ast::FunctionDeclaration &value) {
  std::string signature;
  if (value.is_consuming)
    signature += "consume ";
  signature += "def " + value.name;
  if (!value.type_parameters.empty()) {
    signature += '[';
    for (std::size_t index = 0; index < value.type_parameters.size(); ++index) {
      if (index != 0)
        signature += ", ";
      signature += value.type_parameters[index];
    }
    signature += ']';
  }
  signature += '(';
  for (std::size_t index = 0; index < value.parameters.size(); ++index) {
    if (index != 0)
      signature += ", ";
    signature += value.parameters[index].name + " : " +
                 type_name(value.parameters[index].type);
  }
  if (value.is_variadic) {
    if (!value.parameters.empty())
      signature += ", ";
    signature += "...";
  }
  signature += ") : " + type_name(value.return_type);
  return signature;
}

std::string value_signature(const janus::ast::ValueDeclaration &value) {
  return std::string{value.is_mutable ? "var " : "val "} + value.name + " : " +
         type_name(value.declared_type);
}

std::string type_signature(const janus::ast::ClassDeclaration &value) {
  std::string signature = value.is_value_type ? "struct " : "class ";
  signature += value.name;
  if (!value.type_parameters.empty()) {
    signature += '[';
    for (std::size_t index = 0; index < value.type_parameters.size(); ++index) {
      if (index != 0)
        signature += ", ";
      signature += value.type_parameters[index];
    }
    signature += ']';
  }
  return signature;
}

std::string enum_signature(const janus::ast::EnumDeclaration &value) {
  std::string signature = "enum " + value.name;
  if (!value.type_parameters.empty()) {
    signature += '[';
    for (std::size_t index = 0; index < value.type_parameters.size(); ++index) {
      if (index != 0)
        signature += ", ";
      signature += value.type_parameters[index];
    }
    signature += ']';
  }
  return signature;
}

std::string trait_signature(const janus::ast::TraitDeclaration &value) {
  std::string signature = "trait " + value.name;
  if (!value.type_parameters.empty()) {
    signature += '[';
    for (std::size_t index = 0; index < value.type_parameters.size(); ++index) {
      if (index != 0)
        signature += ", ";
      signature += value.type_parameters[index];
    }
    signature += ']';
  }
  return signature;
}

void add_symbol(std::vector<Symbol> &symbols, std::string module,
                std::string name, std::string kind, std::string signature,
                std::string documentation, std::string parent = {},
                std::vector<DocumentedParameter> parameters = {},
                std::string return_type = {}, bool is_function = false) {
  const std::string qualified =
      parent.empty() ? module + '.' + name : parent + '.' + name;
  StructuredDocumentation structured = parse_documentation(documentation);
  for (DocumentedParameter &parameter : parameters) {
    const auto found =
        structured.parameter_descriptions.find(parameter.name);
    if (found != structured.parameter_descriptions.end())
      parameter.description = found->second;
  }
  symbols.push_back(
      Symbol{std::move(module), std::move(name), qualified, std::move(kind),
             std::move(signature), std::move(documentation),
             std::move(structured), std::move(parameters),
             std::move(return_type), is_function, anchor_for(qualified),
             std::move(parent)});
}

std::vector<DocumentedParameter>
function_parameters(const janus::ast::FunctionDeclaration &function) {
  std::vector<DocumentedParameter> parameters;
  for (const auto &parameter : function.parameters)
    parameters.push_back({parameter.name, type_name(parameter.type), {}});
  return parameters;
}

std::vector<Symbol>
public_symbols(const std::vector<janus::ast::Program> &programs) {
  std::vector<Symbol> symbols;
  for (const janus::ast::Program &program : programs) {
    const std::string module = program.module_name.value_or("root");
    for (const janus::ast::GlobalDeclaration &global : program.globals) {
      if (!global.declaration.is_private && !global.declaration.is_internal) {
        add_symbol(symbols, module, global.declaration.name, "global",
                   value_signature(global.declaration),
                   global.declaration.documentation);
      }
    }
    for (const janus::ast::TraitDeclaration &trait : program.traits) {
      if (trait.is_private)
        continue;
      const std::string parent = module + '.' + trait.name;
      add_symbol(symbols, module, trait.name, "trait", trait_signature(trait),
                 trait.documentation);
      for (const janus::ast::FunctionDeclaration &method : trait.methods) {
        if (!method.is_private && !method.is_internal)
          add_symbol(symbols, module, method.name, "method",
                     function_signature(method), method.documentation, parent,
                     function_parameters(method), type_name(method.return_type),
                     true);
      }
    }
    for (const janus::ast::EnumDeclaration &enumeration : program.enums) {
      if (enumeration.is_private)
        continue;
      const std::string parent = module + '.' + enumeration.name;
      add_symbol(symbols, module, enumeration.name, "enum",
                 enum_signature(enumeration), enumeration.documentation);
      for (const janus::ast::EnumDeclaration::Case &variant :
           enumeration.cases) {
        std::string signature = variant.name;
        if (!variant.payload_types.empty()) {
          signature += '(';
          for (std::size_t index = 0; index < variant.payload_types.size();
               ++index) {
            if (index != 0)
              signature += ", ";
            signature += type_name(variant.payload_types[index]);
          }
          signature += ')';
        }
        add_symbol(symbols, module, variant.name, "variant",
                   std::move(signature), variant.documentation, parent);
      }
    }
    for (const janus::ast::ClassDeclaration &type : program.classes) {
      if (type.is_private)
        continue;
      const std::string parent = module + '.' + type.name;
      add_symbol(symbols, module, type.name,
                 type.is_value_type ? "struct" : "class", type_signature(type),
                 type.documentation);
      const auto append_fields = [&](const auto &fields) {
        for (const janus::ast::ValueDeclaration &field : fields) {
          if (!field.is_private && !field.is_internal)
            add_symbol(symbols, module, field.name, "field",
                       value_signature(field), field.documentation, parent);
        }
      };
      append_fields(type.constructor_fields);
      append_fields(type.fields);
      for (const janus::ast::FunctionDeclaration &method : type.methods) {
        if (!method.is_private && !method.is_internal)
          add_symbol(symbols, module, method.name, "method",
                     function_signature(method), method.documentation, parent,
                     function_parameters(method), type_name(method.return_type),
                     true);
      }
    }
    for (const janus::ast::FunctionDeclaration &function : program.functions) {
      if (!function.is_private && !function.is_internal)
        add_symbol(symbols, module, function.name, "function",
                   function_signature(function), function.documentation, {},
                   function_parameters(function),
                   type_name(function.return_type), true);
    }
  }
  std::sort(symbols.begin(), symbols.end(),
            [](const Symbol &left, const Symbol &right) {
              return std::tie(left.qualified_name, left.kind, left.signature) <
                     std::tie(right.qualified_name, right.kind,
                              right.signature);
            });
  std::set<std::string> used_anchors;
  for (Symbol &symbol : symbols) {
    if (used_anchors.insert(symbol.anchor).second)
      continue;
    const std::string disambiguated_base =
        symbol.anchor + '-' + anchor_for(symbol.kind);
    std::string candidate = disambiguated_base;
    std::size_t occurrence = 2;
    while (!used_anchors.insert(candidate).second)
      candidate = disambiguated_base + '-' + std::to_string(occurrence++);
    symbol.anchor = std::move(candidate);
  }
  return symbols;
}

std::string render_documentation(
    std::string_view documentation, std::string_view context,
    const std::map<std::string, std::vector<std::string>> &links,
    std::vector<janus::driver::UnresolvedDocumentationLink> &unresolved,
    bool strict_links) {
  std::string rendered;
  std::size_t position = 0;
  while (position < documentation.size()) {
    const std::size_t opening = documentation.find("[[", position);
    if (opening == std::string_view::npos) {
      rendered += html_escape(documentation.substr(position));
      break;
    }
    rendered += html_escape(documentation.substr(position, opening - position));
    const std::size_t closing = documentation.find("]]", opening + 2);
    if (closing == std::string_view::npos) {
      rendered += html_escape(documentation.substr(opening));
      break;
    }
    const std::string name{
        documentation.substr(opening + 2, closing - opening - 2)};
    const auto found = links.find(name);
    if (found != links.end() && found->second.size() == 1) {
      rendered += "<a href=\"#" + found->second.front() + "\"><code>" +
                  html_escape(name) + "</code></a>";
    } else {
      rendered += "<code class=\"unresolved\">" + html_escape(name) + "</code>";
      if (strict_links)
        unresolved.push_back({name, std::string{context}});
    }
    position = closing + 2;
  }
  std::string with_breaks;
  for (const char character : rendered) {
    if (character == '\n')
      with_breaks += "<br>\n";
    else
      with_breaks += character;
  }
  return with_breaks;
}

void write_text(const std::filesystem::path &path, std::string_view contents) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  if (!output)
    throw std::runtime_error{"cannot create documentation file '" +
                             path.string() + "'"};
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output)
    throw std::runtime_error{"cannot write documentation file '" +
                             path.string() + "'"};
}

void write_structured_json(
    std::ostringstream &output, const StructuredDocumentation &documentation,
    const std::vector<DocumentedParameter> &parameters = {},
    std::string_view return_type = {}, bool has_return_type = false) {
  output << ",\"summary\":\"" << json_escape(documentation.summary)
         << "\",\"details\":[";
  for (std::size_t index = 0; index < documentation.details.size(); ++index) {
    if (index != 0)
      output << ',';
    output << '"' << json_escape(documentation.details[index]) << '"';
  }
  output << "],\"parameters\":[";
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (index != 0)
      output << ',';
    output << "{\"name\":\"" << json_escape(parameters[index].name)
           << "\",\"type\":\"" << json_escape(parameters[index].type)
           << "\",\"description\":\""
           << json_escape(parameters[index].description) << "\"}";
  }
  output << "],\"returns\":";
  if (has_return_type && !is_unit_or_void(return_type))
    output << "{\"type\":\"" << json_escape(return_type)
           << "\",\"description\":\""
           << json_escape(documentation.return_description) << "\"}";
  else
    output << "null";
  output << ",\"examples\":[";
  for (std::size_t index = 0; index < documentation.examples.size(); ++index) {
    if (index != 0)
      output << ',';
    output << '"' << json_escape(documentation.examples[index]) << '"';
  }
  output << ']';
}

std::vector<std::filesystem::path>
package_sources(const janus::driver::Manifest &manifest) {
  std::set<std::filesystem::path> unique;
  const std::filesystem::path source_root = manifest.root() / "src";
  if (std::filesystem::is_directory(source_root)) {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(source_root)) {
      if (entry.is_regular_file() && entry.path().extension() == ".janus")
        unique.insert(
            std::filesystem::absolute(entry.path()).lexically_normal());
    }
  }
  if (std::filesystem::is_regular_file(manifest.entry_path()))
    unique.insert(
        std::filesystem::absolute(manifest.entry_path()).lexically_normal());
  return {unique.begin(), unique.end()};
}

std::vector<janus::ast::Program>
parse_sources(const std::vector<std::filesystem::path> &sources,
              std::string_view fallback_module = {}) {
  std::vector<janus::ast::Program> programs;
  for (const std::filesystem::path &source : sources) {
    std::ifstream input{source, std::ios::binary};
    if (!input)
      throw std::runtime_error{"cannot read documentation source '" +
                               source.string() + "'"};
    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    janus::frontend::Parser parser{contents};
    janus::ast::Program program = parser.parse_program();
    if (!program.module_name.has_value() && !fallback_module.empty())
      program.module_name = fallback_module;
    programs.push_back(std::move(program));
  }
  return programs;
}

} // namespace

namespace janus::driver {

DocumentationReport
generate_documentation(const std::vector<ast::Program> &programs,
                      const DocumentationOptions &options,
                      bool strict_links) {
  if (options.package_name.empty())
    throw std::runtime_error{"documentation package name cannot be empty"};
  if (options.output_directory.empty())
    throw std::runtime_error{"documentation output directory cannot be empty"};

  std::filesystem::create_directories(options.output_directory);
  const std::vector<Symbol> symbols = public_symbols(programs);
  std::map<std::string, std::string> module_documentation;
  std::map<std::string, StructuredDocumentation> structured_modules;
  for (const ast::Program &program : programs) {
    const std::string module = program.module_name.value_or("root");
    auto [entry, inserted] =
        module_documentation.emplace(module, program.documentation);
    if (!inserted && entry->second.empty())
      entry->second = program.documentation;
  }
  for (const auto &[module, documentation] : module_documentation)
    structured_modules.emplace(module, parse_documentation(documentation));

  std::map<std::string, std::vector<std::string>> links;
  for (const auto &[module, documentation] : module_documentation)
    static_cast<void>(documentation),
        links[module].push_back("module-" + anchor_for(module));
  for (const Symbol &symbol : symbols) {
    links[symbol.name].push_back(symbol.anchor);
    links[symbol.qualified_name].push_back(symbol.anchor);
    if (!symbol.parent.empty()) {
      const auto last = symbol.parent.find_last_of('.');
      const std::string local_parent =
          last == std::string::npos ? symbol.parent
                                   : symbol.parent.substr(last + 1);
      links[local_parent + "." + symbol.name].push_back(symbol.anchor);
    }
  }
  for (auto &[name, anchors] : links) {
    static_cast<void>(name);
    std::sort(anchors.begin(), anchors.end());
    anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());
  }

  DocumentationReport report;
  report.index_path = options.output_directory / "index.html";
  report.api_index_path = options.output_directory / "api-index.json";
  report.module_count = module_documentation.size();
  report.symbol_count = symbols.size();
  for (const auto &[module, documentation] : module_documentation) {
    if (documentation.empty())
      report.undocumented_modules.push_back(module);
  }
  for (const Symbol &symbol : symbols) {
    if (symbol.documentation.empty())
      report.undocumented_symbols.push_back(symbol.qualified_name);
    if (!symbol.is_function)
      continue;
    std::set<std::string> parameter_names;
    for (const DocumentedParameter &parameter : symbol.parameters)
      parameter_names.insert(parameter.name);
    for (const auto &[name, description] :
         symbol.structured.parameter_descriptions) {
      static_cast<void>(description);
      if (!parameter_names.contains(name))
        report.diagnostics.push_back(
            {"unknown-param", symbol.qualified_name, name,
             "@param '" + name + "' does not name a public parameter"});
    }
    for (const std::string &name : symbol.structured.duplicate_parameters)
      report.diagnostics.push_back(
          {"duplicate-param", symbol.qualified_name, name,
           "@param '" + name + "' is documented more than once"});
    for (const DocumentedParameter &parameter : symbol.parameters) {
      const auto documented =
          symbol.structured.parameter_descriptions.find(parameter.name);
      if (documented == symbol.structured.parameter_descriptions.end() ||
          documented->second.empty())
        report.diagnostics.push_back(
            {"undocumented-param", symbol.qualified_name, parameter.name,
             "public parameter '" + parameter.name + "' is undocumented"});
    }
    if (!is_unit_or_void(symbol.return_type) &&
        (!symbol.structured.has_return ||
         symbol.structured.return_description.empty()))
      report.diagnostics.push_back(
          {"missing-return", symbol.qualified_name, {},
           "non-unit return type '" + symbol.return_type +
               "' requires an @return description"});
  }
  std::sort(report.diagnostics.begin(), report.diagnostics.end(),
            [](const DocumentationDiagnostic &left,
               const DocumentationDiagnostic &right) {
              return std::tie(left.symbol, left.code, left.parameter,
                              left.message) <
                     std::tie(right.symbol, right.code, right.parameter,
                              right.message);
            });

  std::ostringstream html;
  html << R"(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light">
<title>)"
       << html_escape(options.package_name) << ' '
       << html_escape(options.package_version)
       << R"( API</title>
<style>
:root{--ink:#173c50;--ink-strong:#102f40;--top:#123f54;--top-soft:#315d70;--paper:#edf1f3;--panel:#fff;--line:#d9e1e5;--signature:#c8d8e1;--accent:#087b51;--accent-bright:#62e6a7;--flare:#d95c36;--muted:#617784;--shadow:0 2px 9px rgba(16,47,64,.12);--mono:ui-monospace,SFMono-Regular,Consolas,"Liberation Mono",monospace;--sans:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:5.5rem}body{margin:0;background:var(--paper);color:var(--ink);font:13px/1.55 var(--sans)}a{color:#285d7c;text-decoration:underline;text-decoration-thickness:.06em;text-underline-offset:.14em}a:hover{color:var(--accent)}a:focus-visible,input:focus-visible,button:focus-visible{outline:3px solid rgba(98,230,167,.68);outline-offset:2px}code{font-family:var(--mono)}
.topbar{position:sticky;z-index:20;top:0;display:grid;grid-template-columns:minmax(15rem,auto) minmax(16rem,1fr) auto;align-items:center;gap:1.4rem;min-height:49px;padding:.45rem 1.55rem;background:var(--top);border-bottom:8px solid var(--top-soft);box-shadow:0 2px 5px rgba(0,0,0,.18);color:#fff}.brand{display:flex;align-items:baseline;gap:1rem;white-space:nowrap}.brand strong{font-size:16px;font-weight:500}.brand .version{color:#b9cad2;font:11px var(--mono)}.search{position:relative;max-width:75rem}.search svg{position:absolute;left:.7rem;top:50%;width:17px;height:17px;transform:translateY(-50%);fill:#9db3be;pointer-events:none}.search input{width:100%;height:30px;border:0;border-radius:2px;padding:.3rem .7rem .3rem 2.15rem;background:#42697b;color:#fff;font:13px var(--sans)}.search input::placeholder{color:#b9c8cf}.search-status{min-width:6rem;color:#c5d5dc;font:11px var(--mono);text-align:right}.nav-toggle{display:none;border:1px solid #73909d;background:transparent;color:#fff;border-radius:3px;padding:.35rem .55rem}
.doc-layout{display:grid;grid-template-columns:minmax(0,1fr) 15rem;gap:1.7rem;width:min(1120px,calc(100% - 3rem));margin:0 auto;padding:2.2rem 0 0}.content{min-width:0}.hero{display:flex;align-items:center;gap:1rem;margin:0 0 1rem;padding:.1rem .75rem}.hero-mark{display:grid;place-items:center;width:64px;height:64px;flex:0 0 64px;border-radius:50%;background:#297896;color:#fff;box-shadow:var(--shadow);font:38px/1 Georgia,serif}.hero h1{margin:0;color:var(--ink-strong);font-size:clamp(25px,3vw,32px);font-weight:400;line-height:1.1}.hero p{margin:.3rem 0 0;color:var(--muted);font:12px var(--mono)}
.module{margin:0 0 2.5rem;scroll-margin-top:5.5rem}.module[hidden],.symbol-card[hidden],.module-link[hidden]{display:none}.module-heading{display:flex;align-items:center;gap:.55rem;min-height:34px;margin:0 0 1rem;padding:.4rem .7rem;background:var(--signature);border-radius:2px;color:var(--ink-strong);font:13px var(--mono)}.module-heading .keyword{font-weight:400}.module-heading h2{display:inline;margin:0;font:700 13px var(--mono)}.module-summary{margin:0 0 1rem;padding:0 .15rem}.module-summary p{margin:.45rem 0}.member-heading{margin:1.7rem 0 .65rem;padding-left:1rem;color:var(--ink-strong);font-size:15px}.member-count{margin-left:.4rem;color:var(--muted);font:11px var(--mono)}
.symbol-list{display:grid;gap:6px}.symbol-card{position:relative;display:grid;grid-template-columns:2rem minmax(0,1fr);gap:.7rem;min-width:0;padding:.65rem .85rem .7rem;background:var(--panel);border-left:3px solid var(--kind-color,#2d7f9e);border-radius:2px;box-shadow:var(--shadow);scroll-margin-top:5.5rem}.symbol-card.child{margin-left:1.6rem}.kind-icon{display:grid;place-items:center;width:19px;height:19px;margin-top:.1rem;border-radius:50%;background:var(--kind-color,#2d7f9e);color:#fff;font:700 10px var(--mono);text-transform:uppercase}.kind{display:block;margin-bottom:.2rem;color:var(--muted);font:10px var(--mono);letter-spacing:.08em;text-transform:uppercase}.owner{color:var(--muted);font:11px var(--mono)}.symbol-card h3{margin:0;color:var(--ink-strong);font-size:13px;font-weight:400;line-height:1.5}.symbol-card h3 code{overflow-wrap:anywhere}.symbol-card p{margin:.35rem 0 0;color:#284e62}.doc-summary{font-weight:500}.doc-details p{margin:.45rem 0}.doc-section{margin-top:.8rem}.doc-section h4{margin:0 0 .3rem;color:var(--ink-strong);font-size:12px}.parameters{width:100%;border-collapse:collapse}.parameters th,.parameters td{padding:.3rem .45rem;border-top:1px solid var(--line);text-align:left;vertical-align:top}.parameters th{color:var(--muted);font-size:10px;text-transform:uppercase}.example{overflow:auto;margin:.35rem 0 0;padding:.65rem .75rem;background:#102f40;color:#f3f7f8;border-radius:2px;white-space:pre}.example a{color:var(--accent-bright)}.symbol-card .qualified{margin-top:.6rem;color:#78909c;font:10px var(--mono)}.symbol-card .permalink{position:absolute;right:.55rem;top:.45rem;opacity:0;color:#7b929d;text-decoration:none}.symbol-card:hover .permalink,.symbol-card .permalink:focus{opacity:1}.kind-class,.kind-struct,.kind-trait,.kind-enum{--kind-color:#087b51}.kind-function,.kind-method{--kind-color:#168db2}.kind-global,.kind-field{--kind-color:#7d5aa6}.kind-variant{--kind-color:#d4673e}.unresolved{color:#b42318;background:#fff0ee;padding:.05rem .2rem;border-radius:2px}
.sidebar{position:sticky;top:5.4rem;align-self:start;max-height:calc(100vh - 6.3rem);overflow:auto;padding:.2rem 0 1.5rem}.sidebar h2{margin:0 0 .45rem;color:var(--ink-strong);font-size:13px;font-weight:500}.module-nav{display:grid}.module-link{display:flex;align-items:center;gap:.45rem;padding:.22rem .4rem;border-left:3px solid transparent;color:#315b75;font:12px var(--mono);text-decoration:none}.module-link:hover{background:#e2e9ec}.module-link.active{border-left-color:#62cce7;color:var(--ink-strong);background:#e6edef}.module-link .dot{width:6px;height:6px;border-radius:50%;background:#4e9fb8}.legend{margin-top:1.4rem;padding-top:.8rem;border-top:1px solid #cfdbdf}.legend p{margin:.25rem 0;color:var(--muted);font:10px var(--mono)}.legend i{display:inline-grid;place-items:center;width:14px;height:14px;margin-right:.35rem;border-radius:50%;background:var(--kind-color);color:#fff;font:8px var(--mono);font-style:normal}
.empty-state{display:none;margin:2rem 0;padding:2rem;background:#fff;border:1px solid var(--line);text-align:center}.empty-state.visible{display:block}footer{width:min(1120px,calc(100% - 3rem));margin:0 auto;padding:1rem 15rem 1.5rem 0;color:#7c8d95;text-align:center;font-size:11px}
@media(max-width:800px){.topbar{grid-template-columns:1fr auto;gap:.7rem;padding:.55rem 1rem;border-bottom-width:5px}.brand{grid-column:1}.search{grid-column:1/-1;grid-row:2}.search-status{display:none}.nav-toggle{display:block;grid-column:2;grid-row:1}.doc-layout{grid-template-columns:1fr;width:min(100% - 1.4rem,54rem);padding-top:1.1rem}.sidebar{display:none;position:fixed;z-index:30;inset:96px .7rem auto;max-height:65vh;padding:1rem;background:#fff;border:1px solid var(--line);box-shadow:0 8px 30px rgba(16,47,64,.28)}body.nav-open .sidebar{display:block}.hero{padding:.3rem 0}.hero-mark{width:50px;height:50px;flex-basis:50px;font-size:29px}.symbol-card{grid-template-columns:1.6rem minmax(0,1fr);padding:.7rem .65rem}.module-heading{overflow-wrap:anywhere}footer{width:100%;padding:1rem}}
@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}}
@media print{.topbar,.sidebar,.permalink{display:none!important}.doc-layout{display:block;width:100%;padding:0}.symbol-card{break-inside:avoid;box-shadow:none;border:1px solid var(--line);border-left:3px solid var(--kind-color)}footer{width:100%;padding:1rem}}
</style>
</head>
<body>
<header class="topbar">
<div class="brand"><strong>)"
       << html_escape(options.package_name) << R"(</strong><span class="version">)"
       << html_escape(options.package_version) << R"(</span></div>
<label class="search"><span class="sr-only" hidden>Search the API</span><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m9.5 3a6.5 6.5 0 1 0 4.1 11.55L19.05 20 20.5 18.55l-5.45-5.45A6.5 6.5 0 0 0 9.5 3Zm0 2a4.5 4.5 0 1 1 0 9 4.5 4.5 0 0 1 0-9Z"/></svg><input id="api-search" type="search" autocomplete="off" placeholder="Search modules, symbols and signatures" aria-controls="api-content"></label>
<div id="search-status" class="search-status" aria-live="polite"></div><button class="nav-toggle" type="button" aria-expanded="false" aria-controls="module-sidebar">Modules</button>
</header>
<div class="doc-layout">
<main id="api-content" class="content">
<header class="hero"><span class="hero-mark" aria-hidden="true">J</span><div><h1>)"
       << html_escape(options.package_name) << R"( API</h1><p>Offline reference · version )"
       << html_escape(options.package_version) << R"(</p></div></header>
<div id="empty-state" class="empty-state"><strong>No API entries match this search.</strong><br>Try a module name, symbol or type.</div>
)";
  for (const auto &[module, documentation] : module_documentation) {
    const std::size_t member_count = static_cast<std::size_t>(std::count_if(
        symbols.begin(), symbols.end(), [&](const Symbol &symbol) {
          return symbol.module == module;
        }));
    html << "<section class=\"module\" id=\"module-" << anchor_for(module)
         << "\" data-module data-search=\""
         << html_escape(module + " " + documentation) << "\">\n"
         << "<header class=\"module-heading\"><span class=\"keyword\">module</span> "
         << "<h2>" << html_escape(module) << "</h2></header>\n"
         << "<div class=\"module-summary\">";
    const StructuredDocumentation &module_doc = structured_modules.at(module);
    if (!module_doc.summary.empty())
      html << "<p class=\"doc-summary\">"
           << render_documentation(module_doc.summary, module, links,
                                   report.unresolved_links, strict_links)
           << "</p>";
    if (!module_doc.details.empty()) {
      html << "<div class=\"doc-details\">";
      for (const std::string &paragraph : module_doc.details)
        html << "<p>"
             << render_documentation(paragraph, module, links,
                                     report.unresolved_links, strict_links)
                                     << "</p>";
      html << "</div>";
    }
    html << "</div><h3 class=\"member-heading\">Public members <span class=\"member-count\">"
         << member_count << "</span></h3>\n<div class=\"symbol-list\">\n";
    for (const Symbol &symbol : symbols) {
      if (symbol.module != module)
        continue;
      const char kind_initial = symbol.kind.empty() ? '?' : symbol.kind.front();
      html << "<article class=\"symbol-card kind-" << html_escape(symbol.kind)
           << (symbol.parent.empty() ? "" : " child")
           << "\" id=\"" << symbol.anchor << "\" data-symbol data-search=\""
           << html_escape(symbol.qualified_name + " " + symbol.kind + " " +
                          symbol.signature + " " + symbol.documentation)
           << "\"><span class=\"kind-icon\" aria-hidden=\"true\">"
           << kind_initial << "</span><div><span class=\"kind\">"
           << html_escape(symbol.kind) << "</span>";
      if (!symbol.parent.empty())
        html << "<div class=\"owner\">Member of "
             << html_escape(symbol.parent) << "</div>";
      html << "<h3><code>"
           << html_escape(symbol.signature) << "</code></h3>\n";
      if (!symbol.structured.summary.empty())
        html << "<p class=\"doc-summary\">"
             << render_documentation(symbol.structured.summary,
                                     symbol.qualified_name, links,
                                     report.unresolved_links, strict_links)
                                     << "</p>\n";
      if (!symbol.structured.details.empty()) {
        html << "<div class=\"doc-details\">";
        for (const std::string &paragraph : symbol.structured.details)
          html << "<p>"
               << render_documentation(paragraph, symbol.qualified_name, links,
                                       report.unresolved_links, strict_links)
                                       << "</p>";
        html << "</div>\n";
      }
      if (!symbol.parameters.empty()) {
        html << "<section class=\"doc-section\"><h4>Parameters</h4>"
                "<table class=\"parameters\"><thead><tr><th>Name</th><th>Type</th>"
                "<th>Description</th></tr></thead><tbody>";
        for (const DocumentedParameter &parameter : symbol.parameters)
          html << "<tr><td><code>" << html_escape(parameter.name)
                << "</code></td><td><code>" << html_escape(parameter.type)
                << "</code></td><td>"
                << render_documentation(parameter.description,
                                        symbol.qualified_name, links,
                                        report.unresolved_links, strict_links)
                                        << "</td></tr>";
        html << "</tbody></table></section>\n";
      }
      if (symbol.is_function && !is_unit_or_void(symbol.return_type)) {
        html << "<section class=\"doc-section\"><h4>Returns</h4><p><code>"
             << html_escape(symbol.return_type) << "</code>";
        if (symbol.structured.has_return)
          html << " — "
               << render_documentation(symbol.structured.return_description,
                                       symbol.qualified_name, links,
                                       report.unresolved_links, strict_links);
        html << "</p></section>\n";
      }
      for (const std::string &example : symbol.structured.examples)
        html << "<section class=\"doc-section\"><h4>Example</h4><pre "
                << "class=\"example\"><code>"
                 << render_documentation(example, symbol.qualified_name, links,
                                         report.unresolved_links, strict_links)
                                         << "</code></pre></section>\n";
      html << "<div class=\"qualified\">" << html_escape(symbol.qualified_name)
           << "</div></div><a class=\"permalink\" href=\"#" << symbol.anchor
           << "\" aria-label=\"Permanent link to " << html_escape(symbol.qualified_name)
           << "\">#</a></article>\n";
    }
    html << "</div></section>\n";
  }
  html << "</main>\n<aside id=\"module-sidebar\" class=\"sidebar\"><h2>Modules</h2>"
          "<nav class=\"module-nav\" aria-label=\"Modules\">";
  for (const auto &[module, documentation] : module_documentation) {
    static_cast<void>(documentation);
    html << "<a class=\"module-link\" data-module-link href=\"#module-"
         << anchor_for(module) << "\"><span class=\"dot\"></span>"
         << html_escape(module) << "</a>";
  }
  html << R"(</nav><div class="legend" aria-label="Symbol legend"><p><i class="kind-class">t</i>types and traits</p><p><i class="kind-function">f</i>functions and methods</p><p><i class="kind-global">v</i>values and fields</p><p><i class="kind-variant">c</i>enum variants</p></div></aside>
</div>
<footer><p>Generated by Janus. No network resources are required.</p></footer>
<script>
(()=>{const input=document.querySelector('#api-search'),status=document.querySelector('#search-status'),empty=document.querySelector('#empty-state'),sections=[...document.querySelectorAll('[data-module]')],links=[...document.querySelectorAll('[data-module-link]')],toggle=document.querySelector('.nav-toggle'),normalize=value=>value.toLocaleLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g,'');let visibleSymbols=document.querySelectorAll('[data-symbol]').length;const update=()=>{const query=normalize(input.value.trim());visibleSymbols=0;let visibleModules=0;sections.forEach(section=>{const moduleMatches=query&&normalize(section.dataset.search).includes(query);let moduleSymbols=0;section.querySelectorAll('[data-symbol]').forEach(card=>{const match=!query||moduleMatches||normalize(card.dataset.search).includes(query);card.hidden=!match;if(match){moduleSymbols++;visibleSymbols++}});section.hidden=Boolean(query)&&moduleSymbols===0;if(!section.hidden)visibleModules++});links.forEach(link=>{const target=document.querySelector(link.hash);link.hidden=Boolean(target&&target.hidden)});empty.classList.toggle('visible',visibleModules===0);status.textContent=query?visibleSymbols+' results':''};input.addEventListener('input',update);toggle.addEventListener('click',()=>{const open=document.body.classList.toggle('nav-open');toggle.setAttribute('aria-expanded',String(open))});document.querySelector('.module-nav').addEventListener('click',()=>{document.body.classList.remove('nav-open');toggle.setAttribute('aria-expanded','false')});const observer=new IntersectionObserver(entries=>{entries.forEach(entry=>{if(!entry.isIntersecting)return;links.forEach(link=>link.classList.toggle('active',link.hash==='#'+entry.target.id))})},{rootMargin:'-20% 0px -70% 0px'});sections.forEach(section=>observer.observe(section));update()})();
</script>
</body>
</html>
)";

  std::sort(report.unresolved_links.begin(), report.unresolved_links.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.symbol, left.context) <
                     std::tie(right.symbol, right.context);
            });
  report.unresolved_links.erase(
      std::unique(
          report.unresolved_links.begin(), report.unresolved_links.end(),
          [](const auto &left, const auto &right) {
            return left.symbol == right.symbol && left.context == right.context;
          }),
      report.unresolved_links.end());

  std::ostringstream index;
  index << "{\n  \"package\": \"" << json_escape(options.package_name)
        << "\",\n  \"version\": \"" << json_escape(options.package_version)
        << "\",\n  \"modules\": [";
  std::size_t module_position = 0;
  for (const auto &[module, documentation] : module_documentation) {
    index << (module_position++ == 0 ? "\n" : ",\n") << "    {\"name\":\""
          << json_escape(module)
          << "\",\"kind\":\"module\",\"documentation\":\""
          << json_escape(documentation) << "\",\"anchor\":\"module-"
          << json_escape(anchor_for(module)) << '"';
    write_structured_json(index, structured_modules.at(module));
    index << '}';
  }
  if (!module_documentation.empty())
    index << '\n';
  index << "  ],\n  \"symbols\": [";
  for (std::size_t position = 0; position < symbols.size(); ++position) {
    const Symbol &symbol = symbols[position];
    index << (position == 0 ? "\n" : ",\n") << "    {\"name\":\""
          << json_escape(symbol.qualified_name) << "\",\"kind\":\""
          << json_escape(symbol.kind) << "\",\"signature\":\""
          << json_escape(symbol.signature) << "\",\"documentation\":\""
          << json_escape(symbol.documentation) << "\",\"anchor\":\""
          << json_escape(symbol.anchor) << '"';
    write_structured_json(index, symbol.structured, symbol.parameters,
                          symbol.return_type, symbol.is_function);
    index << '}';
  }
  if (!symbols.empty())
    index << '\n';
  index << "  ]\n}\n";

  write_text(report.index_path, html.str());
  write_text(report.api_index_path, index.str());
  return report;
}

DocumentationReport
generate_package_documentation(const Manifest &manifest,
                               const std::filesystem::path &output_directory) {
  std::vector<ast::Program> programs =
      parse_sources(package_sources(manifest), manifest.name);
  if (programs.empty())
    throw std::runtime_error{"package contains no Janus source files"};
  return generate_documentation(
      programs, {manifest.name, manifest.version, output_directory}, true);
}

DocumentationReport generate_stdlib_documentation(
    const std::filesystem::path &stdlib_directory,
    const std::filesystem::path &output_directory,
    std::string package_version) {
  const std::filesystem::path source_root = stdlib_directory / "std";
  if (!std::filesystem::is_directory(source_root))
    throw std::runtime_error{"standard library source directory not found: " +
                             source_root.string()};
  std::vector<std::filesystem::path> sources;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(source_root)) {
    if (entry.is_regular_file() && entry.path().extension() == ".janus")
      sources.push_back(
          std::filesystem::absolute(entry.path()).lexically_normal());
  }
  std::sort(sources.begin(), sources.end());
  std::vector<ast::Program> programs = parse_sources(sources);
  if (programs.empty())
    throw std::runtime_error{"standard library contains no Janus source files"};
  for (const ast::Program &program : programs) {
    if (!program.module_name.has_value())
      throw std::runtime_error{
          "every standard-library source must declare its module"};
  }
  return generate_documentation(
      programs, {"Janus standard library", std::move(package_version),
                 output_directory}, false);
}

void open_documentation(const std::filesystem::path &index_path) {
  const std::filesystem::path absolute =
      std::filesystem::absolute(index_path).lexically_normal();
#ifdef _WIN32
  const HINSTANCE result = ShellExecuteW(nullptr, L"open", absolute.c_str(),
                                         nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<std::intptr_t>(result) <= 32)
    throw std::runtime_error{"cannot open generated documentation"};
#else
#ifdef __APPLE__
  const char *application = "open";
#else
  const char *application = std::getenv("BROWSER");
  if (application == nullptr || *application == '\0')
    application = "xdg-open";
#endif
  const pid_t child = fork();
  if (child < 0)
    throw std::runtime_error{"cannot start documentation viewer"};
  if (child == 0) {
    execlp(application, application, absolute.c_str(),
           static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = 0;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0)
    throw std::runtime_error{"documentation viewer failed"};
#endif
}

} // namespace janus::driver
