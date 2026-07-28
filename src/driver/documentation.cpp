#include "janus/driver/documentation.hpp"

#include "janus/frontend/parser.hpp"

#include <algorithm>
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

struct Symbol {
  std::string module;
  std::string name;
  std::string qualified_name;
  std::string kind;
  std::string signature;
  std::string documentation;
  std::string anchor;
  std::string parent;
};

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
                std::string documentation, std::string parent = {}) {
  const std::string qualified =
      parent.empty() ? module + '.' + name : parent + '.' + name;
  symbols.push_back(Symbol{std::move(module), std::move(name), qualified,
                           std::move(kind), std::move(signature),
                           std::move(documentation), anchor_for(qualified),
                           std::move(parent)});
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
                     function_signature(method), method.documentation, parent);
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
                     function_signature(method), method.documentation, parent);
      }
    }
    for (const janus::ast::FunctionDeclaration &function : program.functions) {
      if (!function.is_private && !function.is_internal)
        add_symbol(symbols, module, function.name, "function",
                   function_signature(function), function.documentation);
    }
  }
  std::sort(symbols.begin(), symbols.end(),
            [](const Symbol &left, const Symbol &right) {
              return std::tie(left.qualified_name, left.kind, left.signature) <
                     std::tie(right.qualified_name, right.kind,
                              right.signature);
            });
  return symbols;
}

std::string render_documentation(
    std::string_view documentation, std::string_view context,
    const std::map<std::string, std::vector<std::string>> &links,
    std::vector<janus::driver::UnresolvedDocumentationLink> &unresolved) {
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

} // namespace

namespace janus::driver {

DocumentationReport
generate_documentation(const std::vector<ast::Program> &programs,
                       const DocumentationOptions &options) {
  if (options.package_name.empty())
    throw std::runtime_error{"documentation package name cannot be empty"};
  if (options.output_directory.empty())
    throw std::runtime_error{"documentation output directory cannot be empty"};

  std::filesystem::create_directories(options.output_directory);
  const std::vector<Symbol> symbols = public_symbols(programs);
  std::map<std::string, std::string> module_documentation;
  for (const ast::Program &program : programs) {
    const std::string module = program.module_name.value_or("root");
    auto [entry, inserted] =
        module_documentation.emplace(module, program.documentation);
    if (!inserted && entry->second.empty())
      entry->second = program.documentation;
  }

  std::map<std::string, std::vector<std::string>> links;
  for (const Symbol &symbol : symbols) {
    links[symbol.name].push_back(symbol.anchor);
    links[symbol.qualified_name].push_back(symbol.anchor);
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

  std::ostringstream html;
  html
      << "<!doctype html>\n"
         "<html lang=\"en\">\n"
         "<head>\n"
         "<meta charset=\"utf-8\">\n"
         "<meta name=\"viewport\" "
         "content=\"width=device-width,initial-scale=1\">\n"
      << "<title>" << html_escape(options.package_name) << ' '
      << html_escape(options.package_version)
      << " API</title>\n"
         "<style>"
         "body{font:16px/1.55 system-ui,sans-serif;max-width:72rem;margin:auto;"
         "padding:2rem;color:#17202a}nav a{margin-right:1rem}"
         "section{border-top:1px solid #d8dee4;margin-top:2rem}"
         "article{margin:1.5rem 0;padding-left:1rem;border-left:3px solid "
         "#d8dee4}"
         "code{background:#f3f4f6;padding:.1rem .3rem;border-radius:.2rem}"
         ".kind{color:#57606a;text-transform:uppercase;font-size:.75rem}"
         ".unresolved{color:#b42318}</style>\n"
         "</head>\n<body>\n<header><h1>"
      << html_escape(options.package_name) << " API</h1><p>Version "
      << html_escape(options.package_version)
      << "</p></header>\n<nav aria-label=\"Modules\">";
  for (const auto &[module, documentation] : module_documentation) {
    static_cast<void>(documentation);
    html << "<a href=\"#module-" << anchor_for(module) << "\">"
         << html_escape(module) << "</a>";
  }
  html << "</nav>\n<main>\n";
  for (const auto &[module, documentation] : module_documentation) {
    html << "<section id=\"module-" << anchor_for(module) << "\"><h2>Module "
         << html_escape(module) << "</h2>\n";
    if (!documentation.empty())
      html << "<p>"
           << render_documentation(documentation, module, links,
                                   report.unresolved_links)
           << "</p>\n";
    for (const Symbol &symbol : symbols) {
      if (symbol.module != module)
        continue;
      html << "<article id=\"" << symbol.anchor << "\"><span class=\"kind\">"
           << html_escape(symbol.kind) << "</span><h3><code>"
           << html_escape(symbol.signature) << "</code></h3>\n";
      if (!symbol.documentation.empty())
        html << "<p>"
             << render_documentation(symbol.documentation,
                                     symbol.qualified_name, links,
                                     report.unresolved_links)
             << "</p>\n";
      html << "</article>\n";
    }
    html << "</section>\n";
  }
  html << "</main>\n<footer><p>Generated by Janus. No network resources are "
          "required.</p></footer>\n</body>\n</html>\n";

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
          << json_escape(anchor_for(module)) << "\"}";
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
          << json_escape(symbol.anchor) << "\"}";
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
  std::vector<ast::Program> programs;
  for (const std::filesystem::path &source : package_sources(manifest)) {
    std::ifstream input{source, std::ios::binary};
    if (!input)
      throw std::runtime_error{"cannot read package source '" +
                               source.string() + "'"};
    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    frontend::Parser parser{contents};
    ast::Program program = parser.parse_program();
    if (!program.module_name.has_value())
      program.module_name = manifest.name;
    programs.push_back(std::move(program));
  }
  if (programs.empty())
    throw std::runtime_error{"package contains no Janus source files"};
  return generate_documentation(
      programs, {manifest.name, manifest.version, output_directory});
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
