#include "janus/driver/api_index.hpp"

#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include "llvm/Support/JSON.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace {
using namespace janus;
using namespace janus::driver;

std::string lower(std::string_view value) {
  std::string result{value};
  std::transform(
      result.begin(), result.end(), result.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

std::string trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  return std::string{
      value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1)};
}

std::size_t lexical_distance(std::string_view left, std::string_view right) {
  std::vector<std::size_t> previous(right.size() + 1);
  std::vector<std::size_t> current(right.size() + 1);
  for (std::size_t column = 0; column <= right.size(); ++column)
    previous[column] = column;
  for (std::size_t row = 1; row <= left.size(); ++row) {
    current[0] = row;
    for (std::size_t column = 1; column <= right.size(); ++column)
      current[column] =
          std::min({previous[column] + 1, current[column - 1] + 1,
                    previous[column - 1] +
                        (left[row - 1] == right[column - 1] ? 0U : 1U)});
    std::swap(previous, current);
  }
  return previous.back();
}

std::string type_name(const ast::TypeReference &type) {
  std::string result = type.is_pure_function ? "pure " + type.name : type.name;
  if (!type.type_arguments.empty()) {
    result += '[';
    for (std::size_t i = 0; i < type.type_arguments.size(); ++i) {
      if (i != 0)
        result += ", ";
      result += type_name(type.type_arguments[i]);
    }
    result += ']';
  }
  return result;
}

std::string type_name(const std::optional<ast::TypeReference> &type) {
  return type ? type_name(*type) : std::string{};
}

std::string anchor(std::string_view value) {
  std::string result;
  for (unsigned char c : value) {
    if (std::isalnum(c))
      result += static_cast<char>(std::tolower(c));
    else if (result.empty() || result.back() != '-')
      result += '-';
  }
  while (!result.empty() && result.back() == '-')
    result.pop_back();
  return result;
}

struct Docs {
  std::string summary;
  std::string replacement;
  bool deprecated{};
};

Docs docs(std::string_view text) {
  Docs result;
  std::istringstream lines{std::string{text}};
  std::string line;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line.starts_with("@deprecated")) {
      result.deprecated = true;
      std::string rest = trim(std::string_view{line}.substr(11));
      if (rest.starts_with("use "))
        rest = trim(std::string_view{rest}.substr(4));
      if (rest.starts_with("[[") && rest.ends_with("]]"))
        rest = rest.substr(2, rest.size() - 4);
      result.replacement = rest;
    } else if (!line.empty() && !line.starts_with('@') &&
               result.summary.empty()) {
      result.summary = line;
    }
  }
  return result;
}

std::vector<std::string>
constraints(const std::vector<ast::TypeConstraint> &values) {
  std::vector<std::string> result;
  for (const auto &value : values)
    result.push_back(value.parameter + " : " + type_name(value.trait));
  return result;
}

std::string function_signature(const ast::FunctionDeclaration &fn) {
  std::string result =
      fn.is_constant
          ? "const def "
          : std::string{fn.is_pure ? "pure " : ""} +
                (fn.is_consuming ? "consume def "
                                 : (fn.is_borrowing ? "borrow def " : "def "));
  if (fn.is_tailrec)
    result.insert(result.find("def "), "tailrec ");
  result += fn.name;
  if (!fn.type_parameters.empty()) {
    result += '[';
    for (std::size_t i = 0; i < fn.type_parameters.size(); ++i) {
      if (i)
        result += ", ";
      result += fn.type_parameters[i];
    }
    result += ']';
  }
  result += '(';
  for (std::size_t i = 0; i < fn.parameters.size(); ++i) {
    if (i != 0)
      result += ", ";
    if (fn.parameters[i].is_scoped)
      result += "scoped ";
    if (fn.parameters[i].ownership == ast::ParameterOwnership::Borrow)
      result += "borrow ";
    if (fn.parameters[i].ownership == ast::ParameterOwnership::BorrowMutable)
      result += "borrow var ";
    if (fn.parameters[i].ownership == ast::ParameterOwnership::Consume)
      result += "consume ";
    result += fn.parameters[i].name + " : " + type_name(fn.parameters[i].type);
  }
  if (fn.is_variadic) {
    if (!fn.parameters.empty())
      result += ", ";
    result += "...";
  }
  result += ") : ";
  if (fn.return_ownership == ast::ReturnOwnership::Borrow)
    result += "borrow ";
  else if (fn.return_ownership == ast::ReturnOwnership::BorrowMutable)
    result += "borrow var ";
  else if (fn.return_ownership == ast::ReturnOwnership::Owned)
    result += "owned ";
  result += type_name(fn.return_type);
  return result;
}

void add(ApiIndex &index, std::string module, std::string name,
         std::string kind, std::string signature, std::string documentation,
         std::vector<std::string> generics = {},
         std::vector<std::string> generic_constraints = {},
         std::vector<ApiParameter> parameters = {},
         std::string return_type = {}, std::string parent = {}) {
  const std::string qualified =
      parent.empty() ? module + '.' + name : parent + '.' + name;
  const Docs parsed = docs(documentation);
  ApiSymbol symbol{name,
                   qualified,
                   index.package,
                   module,
                   module,
                   kind,
                   std::move(signature),
                   std::move(generics),
                   std::move(generic_constraints),
                   std::move(parameters),
                   std::move(return_type),
                   parsed.summary,
                   std::move(documentation),
                   "public",
                   "#" + anchor(qualified),
                   parsed.deprecated,
                   std::nullopt};
  if (!parsed.replacement.empty()) {
    symbol.replacement = parsed.replacement.find('.') == std::string::npos
                             ? module + '.' + parsed.replacement
                             : parsed.replacement;
  }
  index.symbols.push_back(std::move(symbol));
}

std::vector<ApiParameter> parameters(const ast::FunctionDeclaration &fn) {
  std::vector<ApiParameter> result;
  for (const auto &parameter : fn.parameters)
    result.push_back({parameter.name, type_name(parameter.type), {}});
  return result;
}

std::vector<ApiParameter>
constructor_parameters(const ast::ClassDeclaration &type) {
  std::vector<ApiParameter> result;
  for (const auto &parameter : type.constructor_parameters)
    result.push_back({parameter.name, type_name(parameter.type), {}});
  for (const auto &field : type.constructor_fields)
    result.push_back({field.name, type_name(field.declared_type),
                      docs(field.documentation).summary});
  return result;
}

std::string type_signature(const ast::ClassDeclaration &type) {
  std::string result = type.is_value_type ? "struct " : "class ";
  result += type.name;
  if (!type.type_parameters.empty()) {
    result += '[';
    for (std::size_t i = 0; i < type.type_parameters.size(); ++i) {
      if (i)
        result += ", ";
      result += type.type_parameters[i];
    }
    result += ']';
  }
  if (type.is_constructor_internal)
    result += " internal";
  result += '(';
  bool first = true;
  for (const auto &parameter : type.constructor_parameters) {
    if (!first)
      result += ", ";
    first = false;
    result += parameter.name + " : " + type_name(parameter.type);
  }
  for (const auto &field : type.constructor_fields) {
    if (!first)
      result += ", ";
    first = false;
    if (field.is_borrowed)
      result += "borrow ";
    result += std::string{field.is_mutable ? "var " : "val "} + field.name +
              " : " + type_name(field.declared_type);
  }
  return result + ')';
}

using ApiIdentity =
    std::tuple<std::string, std::string, std::string, std::string>;

ApiIdentity identity(const ApiSymbol &symbol) {
  return {symbol.package, symbol.qualified_name, symbol.kind, symbol.signature};
}

bool same_symbol(const ApiSymbol &left, const ApiSymbol &right) {
  return left.simple_name == right.simple_name &&
         left.qualified_name == right.qualified_name &&
         left.package == right.package && left.module == right.module &&
         left.required_import == right.required_import &&
         left.kind == right.kind && left.signature == right.signature &&
         left.generic_parameters == right.generic_parameters &&
         left.generic_constraints == right.generic_constraints &&
         left.parameters == right.parameters &&
         left.return_type == right.return_type &&
         left.summary == right.summary &&
         left.documentation == right.documentation &&
         left.visibility == right.visibility &&
         left.documentation_link == right.documentation_link &&
         left.deprecated == right.deprecated &&
         left.replacement == right.replacement;
}

std::string quote(std::string_view value) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : value) {
    switch (c) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (c < 0x20)
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << unsigned(c) << std::dec;
      else
        out << c;
    }
  }
  out << '"';
  return out.str();
}

void write_string_array(std::ostringstream &output,
                        const std::vector<std::string> &values) {
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0)
      output << ',';
    output << quote(values[index]);
  }
  output << ']';
}

std::runtime_error invalid_field(std::string_view field,
                                 std::string_view expected) {
  return std::runtime_error{"API index field '" + std::string{field} +
                            "' must be " + std::string{expected}};
}

std::string required_string(const llvm::json::Object &object,
                            std::string_view key) {
  const auto value = object.getString(key);
  if (!value)
    throw invalid_field(key, "a string");
  return value->str();
}

bool required_boolean(const llvm::json::Object &object, std::string_view key) {
  const auto value = object.getBoolean(key);
  if (!value)
    throw invalid_field(key, "a boolean");
  return *value;
}

const llvm::json::Array &required_array(const llvm::json::Object &object,
                                        std::string_view key) {
  const auto *value = object.getArray(key);
  if (value == nullptr)
    throw invalid_field(key, "an array");
  return *value;
}

std::vector<std::string> required_string_array(const llvm::json::Object &object,
                                               std::string_view key) {
  std::vector<std::string> result;
  for (const llvm::json::Value &value : required_array(object, key)) {
    const auto string = value.getAsString();
    if (!string)
      throw invalid_field(key, "an array of strings");
    result.push_back(string->str());
  }
  return result;
}

ApiParameter parse_parameter(const llvm::json::Value &value) {
  const auto *object = value.getAsObject();
  if (object == nullptr)
    throw invalid_field("parameters", "an array of objects");
  return {required_string(*object, "name"), required_string(*object, "type"),
          required_string(*object, "documentation")};
}

ApiSymbol parse_symbol(const llvm::json::Value &value) {
  const auto *object = value.getAsObject();
  if (object == nullptr)
    throw invalid_field("symbols", "an array of objects");

  ApiSymbol symbol;
  symbol.simple_name = required_string(*object, "simple_name");
  symbol.qualified_name = required_string(*object, "qualified_name");
  symbol.package = required_string(*object, "package");
  symbol.module = required_string(*object, "module");
  symbol.required_import = required_string(*object, "required_import");
  symbol.kind = required_string(*object, "kind");
  symbol.signature = required_string(*object, "signature");
  symbol.generic_parameters =
      required_string_array(*object, "generic_parameters");
  symbol.generic_constraints =
      required_string_array(*object, "generic_constraints");
  for (const llvm::json::Value &parameter :
       required_array(*object, "parameters"))
    symbol.parameters.push_back(parse_parameter(parameter));
  symbol.return_type = required_string(*object, "return_type");
  symbol.summary = required_string(*object, "summary");
  symbol.documentation = required_string(*object, "documentation");
  symbol.visibility = required_string(*object, "visibility");
  symbol.documentation_link = required_string(*object, "documentation_link");
  symbol.deprecated = required_boolean(*object, "deprecated");
  const llvm::json::Value *replacement = object->get("replacement");
  if (replacement == nullptr)
    throw invalid_field("replacement", "a string or null");
  if (const auto string = replacement->getAsString())
    symbol.replacement = string->str();
  else if (replacement->kind() != llvm::json::Value::Null)
    throw invalid_field("replacement", "a string or null");
  return symbol;
}
} // namespace

namespace janus::driver {

ApiIndex build_api_index(const std::vector<ast::Program> &programs,
                         const ApiIndexMetadata &metadata) {
  ApiIndex index;
  index.package = metadata.package;
  index.package_version = metadata.package_version;
  for (const auto &program : programs) {
    const std::string module = program.module_name.value_or("root");
    std::optional<semantic::AnalysisResult> analysis;
    try {
      analysis = semantic::Analyzer{}.analyze(
          program, {.require_entry_point = false, .target = {}});
    } catch (const std::exception &) {
    }
    for (const auto &global : program.globals) {
      if (global.declaration.is_private || global.declaration.is_internal)
        continue;
      add(index, module, global.declaration.name, "global",
          std::string{global.declaration.is_borrowed ? "borrow " : ""} +
              (global.declaration.is_constant
                   ? "const "
                   : (global.declaration.is_mutable ? "var " : "val ")) +
              global.declaration.name + " : " +
              type_name(global.declaration.declared_type),
          global.declaration.documentation);
      if (global.declaration.is_constant && analysis) {
        const std::string origin =
            global.module_name.value_or("root") + "." + global.declaration.name;
        if (const auto value = analysis->global_constant_values.find(origin);
            value != analysis->global_constant_values.end())
          index.symbols.back().signature +=
              " = " + constant::canonical_serialize(value->second) +
              " [origin " + origin + "]";
      }
    }
    for (const auto &function : program.functions) {
      if (function.is_private || function.is_internal)
        continue;
      add(index, module, function.name, "function",
          function_signature(function), function.documentation,
          function.type_parameters, constraints(function.type_constraints),
          parameters(function), type_name(function.return_type));
    }
    for (const auto &trait : program.traits) {
      if (trait.is_private)
        continue;
      const std::string parent = module + '.' + trait.name;
      add(index, module, trait.name, "trait", "trait " + trait.name,
          trait.documentation, trait.type_parameters,
          constraints(trait.type_constraints));
      index.symbols.back().signature = "trait " + trait.name;
      if (!trait.type_parameters.empty()) {
        index.symbols.back().signature += '[';
        for (std::size_t i = 0; i < trait.type_parameters.size(); ++i) {
          if (i)
            index.symbols.back().signature += ", ";
          index.symbols.back().signature += trait.type_parameters[i];
        }
        index.symbols.back().signature += ']';
      }
      for (const auto &associated : trait.associated_types)
        add(index, module, associated.name, "associated-type",
            "type " + associated.name, associated.documentation, {}, {}, {}, {},
            parent);
      for (const auto &function : trait.methods) {
        if (function.is_private || function.is_internal)
          continue;
        add(index, module, function.name, "method",
            function_signature(function), function.documentation,
            function.type_parameters, constraints(function.type_constraints),
            parameters(function), type_name(function.return_type), parent);
      }
    }
    for (const auto &type : program.classes) {
      if (type.is_private)
        continue;
      const std::string parent = module + '.' + type.name;
      add(index, module, type.name, type.is_value_type ? "struct" : "class",
          type_signature(type), type.documentation, type.type_parameters,
          constraints(type.type_constraints), constructor_parameters(type));
      const auto append_fields = [&](const auto &fields) {
        for (const auto &field : fields) {
          if (field.is_private || field.is_internal)
            continue;
          add(index, module, field.name, "field",
              std::string{field.is_borrowed ? "borrow " : ""} +
                  (field.is_mutable ? "var " : "val ") + field.name + " : " +
                  type_name(field.declared_type),
              field.documentation, {}, {}, {}, {}, parent);
        }
      };
      append_fields(type.constructor_fields);
      append_fields(type.fields);
      for (const auto &associated : type.associated_types)
        add(index, module, associated.name, "associated-type",
            "type " + associated.name + " = " +
                type_name(associated.definition),
            associated.documentation, {}, {}, {}, {}, parent);
      for (const auto &function : type.methods) {
        if (function.is_private || function.is_internal)
          continue;
        add(index, module, function.name, "method",
            function_signature(function), function.documentation,
            function.type_parameters, constraints(function.type_constraints),
            parameters(function), type_name(function.return_type), parent);
      }
    }
    for (const auto &extension : program.extensions) {
      if (extension.is_private)
        continue;
      const std::string parent = module + '.' + extension.target_type.name;
      for (std::size_t method_index = 0;
           method_index < extension.methods.size(); ++method_index) {
        const auto &function = extension.methods[method_index];
        std::string signature = function_signature(function);
        if (extension.receiver_ownerships[method_index] ==
            ast::ParameterOwnership::BorrowMutable)
          signature.insert(0, "borrow var ");
        signature += " [extension]";
        std::vector<std::string> generic_parameters = extension.type_parameters;
        generic_parameters.insert(generic_parameters.end(),
                                  function.type_parameters.begin(),
                                  function.type_parameters.end());
        add(index, module, function.name, "method", std::move(signature),
            function.documentation, std::move(generic_parameters),
            constraints(function.type_constraints), parameters(function),
            type_name(function.return_type), parent);
      }
    }
    for (const auto &value : program.enums) {
      if (value.is_private)
        continue;
      const std::string parent = module + '.' + value.name;
      add(index, module, value.name, "enum", "enum " + value.name,
          value.documentation, value.type_parameters);
      if (!value.type_parameters.empty()) {
        index.symbols.back().signature += '[';
        for (std::size_t i = 0; i < value.type_parameters.size(); ++i) {
          if (i)
            index.symbols.back().signature += ", ";
          index.symbols.back().signature += value.type_parameters[i];
        }
        index.symbols.back().signature += ']';
      }
      if (!value.implemented_traits.empty()) {
        index.symbols.back().signature += " extends ";
        for (std::size_t i = 0; i < value.implemented_traits.size(); ++i) {
          if (i)
            index.symbols.back().signature += ", ";
          index.symbols.back().signature +=
              type_name(value.implemented_traits[i]);
        }
      }
      for (const auto &associated : value.associated_types)
        add(index, module, associated.name, "associated-type",
            "type " + associated.name + " = " +
                type_name(associated.definition),
            associated.documentation, {}, {}, {}, {}, parent);
      for (const auto &variant : value.cases) {
        std::string signature = variant.name;
        if (!variant.payload_types.empty()) {
          signature += '(';
          for (std::size_t i = 0; i < variant.payload_types.size(); ++i) {
            if (i)
              signature += ", ";
            signature += type_name(variant.payload_types[i]);
          }
          signature += ')';
        }
        add(index, module, variant.name, "variant", std::move(signature),
            variant.documentation, {}, {}, {}, {}, parent);
      }
    }
  }
  std::sort(index.symbols.begin(), index.symbols.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.qualified_name, left.kind, left.signature,
                              left.package) <
                     std::tie(right.qualified_name, right.kind, right.signature,
                              right.package);
            });
  std::set<std::string> links;
  for (auto &symbol : index.symbols) {
    std::string candidate = symbol.documentation_link;
    std::size_t occurrence = 2;
    while (!links.insert(candidate).second)
      candidate = symbol.documentation_link + '-' + symbol.kind + '-' +
                  std::to_string(occurrence++);
    symbol.documentation_link = candidate;
  }
  return index;
}

ApiIndex build_api_index_from_source_roots(
    const std::vector<std::filesystem::path> &roots,
    const ApiIndexMetadata &metadata) {
  std::set<std::filesystem::path> files;
  for (const auto &root : roots) {
    if (std::filesystem::is_regular_file(root) && root.extension() == ".janus")
      files.insert(root);
    else if (std::filesystem::is_directory(root))
      for (const auto &entry :
           std::filesystem::recursive_directory_iterator(root))
        if (entry.is_regular_file() && entry.path().extension() == ".janus")
          files.insert(entry.path());
  }
  std::vector<ast::Program> programs;
  for (const auto &file : files) {
    std::ifstream input{file, std::ios::binary};
    if (!input)
      throw std::runtime_error{"cannot read API source '" + file.string() +
                               "'"};
    const std::string source{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
    frontend::Parser parser{source};
    programs.push_back(parser.parse_program());
  }
  return build_api_index(programs, metadata);
}

ApiIndex merge_api_indexes(const std::vector<ApiIndex> &indexes) {
  ApiIndex merged;
  merged.package = "merged";
  for (const auto &index : indexes)
    merged.symbols.insert(merged.symbols.end(), index.symbols.begin(),
                          index.symbols.end());
  std::sort(merged.symbols.begin(), merged.symbols.end(),
            [](const auto &left, const auto &right) {
              return identity(left) < identity(right);
            });
  std::vector<ApiSymbol> unique;
  for (auto &symbol : merged.symbols) {
    if (!unique.empty() && identity(unique.back()) == identity(symbol)) {
      if (!same_symbol(unique.back(), symbol))
        throw std::runtime_error{"conflicting duplicate API symbol identity '" +
                                 symbol.package + ":" + symbol.qualified_name +
                                 "'"};
      continue;
    }
    unique.push_back(std::move(symbol));
  }
  merged.symbols = std::move(unique);
  return merged;
}

std::vector<ApiSearchResult> search_api(const ApiIndex &index,
                                        const ApiSearchQuery &query) {
  std::vector<ApiSearchResult> results;
  const std::string query_text = lower(trim(query.text));
  std::istringstream words{query_text};
  std::vector<std::string> terms;
  for (std::string word; words >> word;)
    terms.push_back(word);

  for (const auto &symbol : index.symbols) {
    if (symbol.visibility != "public" ||
        (!query.module.empty() && symbol.module != query.module) ||
        (!query.kind.empty() && symbol.kind != query.kind) ||
        (!query.package.empty() && symbol.package != query.package))
      continue;
    const std::string simple = lower(symbol.simple_name);
    const std::string qualified = lower(symbol.qualified_name);
    const std::string searchable = lower(
        symbol.summary + ' ' + symbol.documentation + ' ' + symbol.signature);
    std::int64_t score = 0;
    std::string reason;
    if (query_text.empty()) {
      reason = "public symbol";
    } else if (query_text == simple) {
      score = 10000;
      reason = "exact simple name";
    } else if (query_text == qualified) {
      score = 9000;
      reason = "exact qualified name";
    } else {
      const bool all_terms_match =
          !terms.empty() &&
          std::all_of(terms.begin(), terms.end(), [&](const auto &term) {
            return simple.find(term) != std::string::npos ||
                   qualified.find(term) != std::string::npos ||
                   searchable.find(term) != std::string::npos;
          });
      if (!all_terms_match) {
        const std::size_t distance = lexical_distance(query_text, simple);
        const std::size_t maximum_distance =
            std::max<std::size_t>(1, (query_text.size() + 2) / 3);
        if (terms.size() != 1 || distance > maximum_distance)
          continue;
        score = 4000 - static_cast<std::int64_t>(distance * 250);
        reason = "lexical proximity";
      } else {
        score = qualified.find(query_text) != std::string::npos ? 5000 : 1000;
        reason = "text";
      }
    }
    if (query.expected_type && symbol.return_type == *query.expected_type)
      score += 500;
    if (query.argument_count &&
        symbol.parameters.size() == *query.argument_count)
      score += 250;
    if (std::find(query.imported_modules.begin(), query.imported_modules.end(),
                  symbol.required_import) != query.imported_modules.end())
      score += 400;
    if (query.generic_argument_count &&
        symbol.generic_parameters.size() == *query.generic_argument_count)
      score += 300;
    results.push_back({&symbol, score, std::move(reason)});
  }
  std::sort(results.begin(), results.end(),
            [](const auto &left, const auto &right) {
              if (left.score != right.score)
                return left.score > right.score;
              return std::tie(left.symbol->qualified_name,
                              left.symbol->signature, left.symbol->package) <
                     std::tie(right.symbol->qualified_name,
                              right.symbol->signature, right.symbol->package);
            });
  return results;
}

std::string serialize_api_index(const ApiIndex &index) {
  std::ostringstream output;
  output << "{\"format_version\":" << index.format_version
         << ",\"package\":" << quote(index.package)
         << ",\"package_version\":" << quote(index.package_version)
         << ",\"symbols\":[";
  for (std::size_t index_position = 0; index_position < index.symbols.size();
       ++index_position) {
    if (index_position != 0)
      output << ',';
    const ApiSymbol &symbol = index.symbols[index_position];
    output << "{\"simple_name\":" << quote(symbol.simple_name)
           << ",\"qualified_name\":" << quote(symbol.qualified_name)
           << ",\"package\":" << quote(symbol.package)
           << ",\"module\":" << quote(symbol.module)
           << ",\"required_import\":" << quote(symbol.required_import)
           << ",\"kind\":" << quote(symbol.kind)
           << ",\"signature\":" << quote(symbol.signature)
           << ",\"generic_parameters\":";
    write_string_array(output, symbol.generic_parameters);
    output << ",\"generic_constraints\":";
    write_string_array(output, symbol.generic_constraints);
    output << ",\"parameters\":[";
    for (std::size_t parameter_position = 0;
         parameter_position < symbol.parameters.size(); ++parameter_position) {
      if (parameter_position != 0)
        output << ',';
      const ApiParameter &parameter = symbol.parameters[parameter_position];
      output << "{\"name\":" << quote(parameter.name)
             << ",\"type\":" << quote(parameter.type)
             << ",\"documentation\":" << quote(parameter.documentation) << '}';
    }
    output << "],\"return_type\":" << quote(symbol.return_type)
           << ",\"summary\":" << quote(symbol.summary)
           << ",\"documentation\":" << quote(symbol.documentation)
           << ",\"visibility\":" << quote(symbol.visibility)
           << ",\"documentation_link\":" << quote(symbol.documentation_link)
           << ",\"deprecated\":" << (symbol.deprecated ? "true" : "false")
           << ",\"replacement\":"
           << (symbol.replacement ? quote(*symbol.replacement) : "null") << '}';
  }
  output << "]}\n";
  return output.str();
}

ApiIndex parse_api_index(std::string_view json) {
  auto parsed = llvm::json::parse(json);
  if (!parsed)
    throw std::runtime_error{"invalid API index JSON"};
  const auto *object = parsed->getAsObject();
  if (object == nullptr)
    throw std::runtime_error{"API index must be an object"};

  const auto format_version = object->getInteger("format_version");
  if (!format_version || *format_version < 0 ||
      static_cast<std::uint64_t>(*format_version) >
          std::numeric_limits<std::uint32_t>::max())
    throw invalid_field("format_version", "a non-negative 32-bit integer");

  ApiIndex index;
  index.format_version = static_cast<std::uint32_t>(*format_version);
  if (index.format_version != api_index_format_version)
    throw std::runtime_error{"unsupported API index format version"};
  index.package = required_string(*object, "package");
  index.package_version = required_string(*object, "package_version");
  for (const llvm::json::Value &symbol : required_array(*object, "symbols"))
    index.symbols.push_back(parse_symbol(symbol));
  std::map<ApiIdentity, ApiSymbol> seen;
  for (const ApiSymbol &symbol : index.symbols) {
    if (index.package != "merged" && symbol.package != index.package)
      throw std::runtime_error{"API symbol package '" + symbol.package +
                               "' does not match index package '" +
                               index.package + "'"};
    const auto [found, inserted] = seen.emplace(identity(symbol), symbol);
    if (!inserted && !same_symbol(found->second, symbol))
      throw std::runtime_error{"conflicting duplicate API symbol identity '" +
                               symbol.package + ":" + symbol.qualified_name +
                               "'"};
  }
  return index;
}
ApiIndex load_api_index(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  if (!input)
    throw std::runtime_error{"cannot read API index '" + path.string() + "'"};
  const std::string contents{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
  return parse_api_index(contents);
}

void write_api_index(const ApiIndex &index, const std::filesystem::path &path) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  if (!output)
    throw std::runtime_error{"cannot write API index '" + path.string() + "'"};
  output << serialize_api_index(index);
}

std::string format_api_search(const std::vector<ApiSearchResult> &results,
                              std::string_view format) {
  std::ostringstream output;
  if (format == "json") {
    output << "{\"format_version\":1,\"results\":[";
    for (std::size_t index = 0; index < results.size(); ++index) {
      if (index != 0)
        output << ',';
      const ApiSymbol &symbol = *results[index].symbol;
      output << "{\"score\":" << results[index].score
             << ",\"reason\":" << quote(results[index].reason)
             << ",\"simple_name\":" << quote(symbol.simple_name)
             << ",\"qualified_name\":" << quote(symbol.qualified_name)
             << ",\"package\":" << quote(symbol.package)
             << ",\"module\":" << quote(symbol.module)
             << ",\"required_import\":" << quote(symbol.required_import)
             << ",\"kind\":" << quote(symbol.kind)
             << ",\"signature\":" << quote(symbol.signature)
             << ",\"summary\":" << quote(symbol.summary)
             << ",\"documentation_link\":" << quote(symbol.documentation_link)
             << ",\"deprecated\":" << (symbol.deprecated ? "true" : "false")
             << '}';
    }
    output << "]}\n";
    return output.str();
  }

  for (const ApiSearchResult &result : results) {
    const ApiSymbol &symbol = *result.symbol;
    output << symbol.qualified_name << " — " << symbol.signature << '\n'
           << "  import " << symbol.required_import << " | " << symbol.package;
    if (!symbol.summary.empty())
      output << " | " << symbol.summary;
    if (!symbol.documentation_link.empty())
      output << " | " << symbol.documentation_link;
    if (symbol.deprecated)
      output << " | deprecated";
    output << '\n';
  }
  return output.str();
}

} // namespace janus::driver
