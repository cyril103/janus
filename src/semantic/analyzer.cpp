#include "janus/semantic/analyzer.hpp"

#include "janus/constant/evaluator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/ownership/classifier.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace {

constexpr std::size_t enum_arity_marker =
    std::numeric_limits<std::size_t>::max() / 2;
constexpr std::size_t ambiguous_arity_marker =
    std::numeric_limits<std::size_t>::max();

const janus::Type *builtin_type(std::string_view name) {
  if (name == "int")
    return &janus::Type::int_type();
  if (name == "uint")
    return &janus::Type::uint_type();
  if (name == "long")
    return &janus::Type::long_type();
  if (name == "ulong")
    return &janus::Type::ulong_type();
  if (name == "float")
    return &janus::Type::float_type();
  if (name == "double")
    return &janus::Type::double_type();
  if (name == "byte")
    return &janus::Type::byte_type();
  if (name == "ubyte")
    return &janus::Type::ubyte_type();
  if (name == "short")
    return &janus::Type::short_type();
  if (name == "ushort")
    return &janus::Type::ushort_type();
  if (name == "char")
    return &janus::Type::char_type();
  if (name == "bool")
    return &janus::Type::bool_type();
  if (name == "string")
    return &janus::Type::string_type();
  if (name == "Unit")
    return &janus::Type::unit_type();
  if (name == "isize")
    return &janus::Type::isize_type();
  if (name == "usize")
    return &janus::Type::usize_type();
  return nullptr;
}

bool has_derivation(const std::vector<janus::ast::Derivation> &derivations,
                    janus::ast::DerivationKind kind) {
  return std::any_of(
      derivations.begin(), derivations.end(),
      [kind](const janus::ast::Derivation &item) { return item.kind == kind; });
}

std::string_view derivation_name(janus::ast::DerivationKind kind) {
  switch (kind) {
  case janus::ast::DerivationKind::Copy:
    return "Copy";
  case janus::ast::DerivationKind::Equality:
    return "Equality";
  case janus::ast::DerivationKind::Hashing:
    return "Hashing";
  case janus::ast::DerivationKind::Debug:
    return "Debug";
  }
  return "";
}

std::optional<janus::ast::DerivationKind>
derivation_constraint(std::string_view name) {
  if (name == "Copy")
    return janus::ast::DerivationKind::Copy;
  if (name == "Equality")
    return janus::ast::DerivationKind::Equality;
  if (name == "Hashing")
    return janus::ast::DerivationKind::Hashing;
  if (name == "Debug")
    return janus::ast::DerivationKind::Debug;
  return std::nullopt;
}

janus::semantic::SemanticType resolve_type(
    const janus::ast::TypeReference &reference,
    const std::unordered_set<std::string> &type_parameters,
    const std::unordered_map<std::string, std::size_t> *class_arities = nullptr,
    const std::optional<std::string> &context_module = std::nullopt,
    const std::unordered_set<std::string> *scoped_type_aliases = nullptr) {
  if (const janus::Type *type = builtin_type(reference.name)) {
    if (!reference.type_arguments.empty())
      throw janus::CompileError{reference.location,
                                "built-in type '" + reference.name +
                                    "' does not accept type arguments"};
    return janus::semantic::SemanticType{type, {}};
  }
  if (reference.name == "Function") {
    if (reference.type_arguments.empty())
      throw janus::CompileError{reference.location,
                                "a function type must declare a return type"};
    std::vector<janus::semantic::SemanticType> signature;
    signature.reserve(reference.type_arguments.size());
    for (const janus::ast::TypeReference &argument : reference.type_arguments)
      signature.push_back(resolve_type(argument, type_parameters, class_arities,
                                       context_module, scoped_type_aliases));
    for (std::size_t index = 0; index + 1 < signature.size(); ++index) {
      if (signature[index].is_concrete() &&
          signature[index].concrete->kind() == janus::TypeKind::Unit)
        throw janus::CompileError{
            reference.location,
            "Unit cannot be used as a function parameter type"};
    }
    return janus::semantic::SemanticType{
        nullptr, "Function", false, std::move(signature), false, false, true};
  }
  if (type_parameters.contains(reference.name)) {
    if (!reference.type_arguments.empty())
      throw janus::CompileError{reference.location,
                                "type parameter '" + reference.name +
                                    "' does not accept type arguments"};
    return janus::semantic::SemanticType{nullptr, reference.name};
  }
  if (reference.name == "Ptr") {
    if (reference.type_arguments.size() != 1)
      throw janus::CompileError{
          reference.location,
          "Ptr expects exactly one type argument, got " +
              std::to_string(reference.type_arguments.size())};
    janus::semantic::SemanticType element =
        resolve_type(reference.type_arguments.front(), type_parameters,
                     class_arities, context_module, scoped_type_aliases);
    if (element.is_concrete() &&
        element.concrete->kind() == janus::TypeKind::Unit)
      throw janus::CompileError{reference.location,
                                "Ptr[Unit] is not a valid pointer type"};
    return janus::semantic::SemanticType{
        nullptr, "Ptr", false, {std::move(element)}, true};
  }
  if (class_arities != nullptr) {
    auto iterator = class_arities->find(
        context_module.has_value() ? *context_module + "." + reference.name
                                   : reference.name);
    if (iterator == class_arities->end())
      iterator = class_arities->find(reference.name);
    if (iterator != class_arities->end()) {
      if (iterator->second == ambiguous_arity_marker)
        throw janus::CompileError{reference.location,
                                  "type name '" + reference.name +
                                      "' is ambiguous; use a qualified name"};
      if (iterator->second >= enum_arity_marker) {
        const std::size_t arity = iterator->second - enum_arity_marker;
        if (reference.type_arguments.size() != arity)
          throw janus::CompileError{
              reference.location,
              "enum '" + reference.name + "' expects " + std::to_string(arity) +
                  " type argument(s), got " +
                  std::to_string(reference.type_arguments.size())};
        std::vector<janus::semantic::SemanticType> arguments;
        for (const janus::ast::TypeReference &argument :
             reference.type_arguments)
          arguments.push_back(resolve_type(argument, type_parameters,
                                           class_arities, context_module,
                                           scoped_type_aliases));
        const std::string identity =
            scoped_type_aliases != nullptr &&
                    scoped_type_aliases->contains(iterator->first)
                ? iterator->first
                : reference.name;
        return janus::semantic::SemanticType{
            nullptr, identity, false, std::move(arguments), false, true};
      }
      if (reference.type_arguments.size() != iterator->second)
        throw janus::CompileError{
            reference.location,
            "class '" + reference.name + "' expects " +
                std::to_string(iterator->second) + " type argument(s), got " +
                std::to_string(reference.type_arguments.size())};
      std::vector<janus::semantic::SemanticType> arguments;
      arguments.reserve(reference.type_arguments.size());
      for (const janus::ast::TypeReference &argument :
           reference.type_arguments) {
        arguments.push_back(resolve_type(argument, type_parameters,
                                         class_arities, context_module,
                                         scoped_type_aliases));
      }
      const std::string identity =
          scoped_type_aliases != nullptr &&
                  scoped_type_aliases->contains(iterator->first)
              ? iterator->first
              : reference.name;
      return janus::semantic::SemanticType{nullptr, identity, true,
                                           std::move(arguments)};
    }
  }
  throw janus::CompileError{reference.location,
                            "unknown type '" + reference.name + "'"};
}

bool same_type(const janus::semantic::SemanticType &left,
               const janus::semantic::SemanticType &right) {
  if (left.is_concrete() != right.is_concrete())
    return false;
  if (left.is_class() || right.is_class())
    return left.is_class() && right.is_class() &&
           left.parameter == right.parameter &&
           left.type_arguments.size() == right.type_arguments.size() &&
           std::equal(left.type_arguments.begin(), left.type_arguments.end(),
                      right.type_arguments.begin(), same_type);
  if (left.is_enum() || right.is_enum())
    return left.is_enum() && right.is_enum() &&
           left.parameter == right.parameter &&
           left.type_arguments.size() == right.type_arguments.size() &&
           std::equal(left.type_arguments.begin(), left.type_arguments.end(),
                      right.type_arguments.begin(), same_type);
  if (left.is_function() || right.is_function())
    return left.is_function() && right.is_function() &&
           left.type_arguments.size() == right.type_arguments.size() &&
           std::equal(left.type_arguments.begin(), left.type_arguments.end(),
                      right.type_arguments.begin(), same_type);
  if (left.is_pointer() || right.is_pointer())
    return left.is_pointer() && right.is_pointer() &&
           left.type_arguments.size() == 1 &&
           right.type_arguments.size() == 1 &&
           same_type(left.type_arguments.front(), right.type_arguments.front());
  return left.is_concrete() ? left.concrete->kind() == right.concrete->kind()
                            : left.parameter == right.parameter;
}

bool is_scalar_cast_type(const janus::semantic::SemanticType &type) {
  if (type.is_enum())
    return true;
  if (!type.is_concrete())
    return false;
  switch (type.concrete->kind()) {
  case janus::TypeKind::Int:
  case janus::TypeKind::UInt:
  case janus::TypeKind::Long:
  case janus::TypeKind::ULong:
  case janus::TypeKind::Float:
  case janus::TypeKind::Double:
  case janus::TypeKind::Byte:
  case janus::TypeKind::UByte:
  case janus::TypeKind::Short:
  case janus::TypeKind::UShort:
  case janus::TypeKind::ISize:
  case janus::TypeKind::Char:
  case janus::TypeKind::Bool:
  case janus::TypeKind::USize:
    return true;
  default:
    return false;
  }
}

bool is_integer_cast_type(const janus::semantic::SemanticType &type) {
  if (type.is_enum())
    return true;
  if (!type.is_concrete())
    return false;
  switch (type.concrete->kind()) {
  case janus::TypeKind::Int:
  case janus::TypeKind::UInt:
  case janus::TypeKind::Long:
  case janus::TypeKind::ULong:
  case janus::TypeKind::Byte:
  case janus::TypeKind::UByte:
  case janus::TypeKind::Short:
  case janus::TypeKind::UShort:
  case janus::TypeKind::ISize:
  case janus::TypeKind::Char:
  case janus::TypeKind::Bool:
  case janus::TypeKind::USize:
    return true;
  default:
    return false;
  }
}

bool is_c_abi_type(const janus::semantic::SemanticType &type, bool allow_unit) {
  if (type.is_pointer())
    return true;
  if (!type.is_concrete())
    return false;
  switch (type.concrete->kind()) {
  case janus::TypeKind::Int:
  case janus::TypeKind::UInt:
  case janus::TypeKind::Long:
  case janus::TypeKind::ULong:
  case janus::TypeKind::Float:
  case janus::TypeKind::Double:
  case janus::TypeKind::Byte:
  case janus::TypeKind::UByte:
  case janus::TypeKind::Short:
  case janus::TypeKind::UShort:
  case janus::TypeKind::ISize:
  case janus::TypeKind::Char:
  case janus::TypeKind::Bool:
  case janus::TypeKind::USize:
    return true;
  case janus::TypeKind::Unit:
    return allow_unit;
  case janus::TypeKind::String:
  case janus::TypeKind::Enum:
  case janus::TypeKind::Function:
  case janus::TypeKind::Pointer:
  case janus::TypeKind::Class:
  case janus::TypeKind::Struct:
    return false;
  }
  return false;
}

bool is_c_variadic_type(const janus::semantic::SemanticType &type) {
  if (type.is_pointer())
    return true;
  if (!type.is_concrete())
    return false;
  switch (type.concrete->kind()) {
  case janus::TypeKind::Int:
  case janus::TypeKind::UInt:
  case janus::TypeKind::Long:
  case janus::TypeKind::ULong:
  case janus::TypeKind::Float:
  case janus::TypeKind::Double:
  case janus::TypeKind::Byte:
  case janus::TypeKind::UByte:
  case janus::TypeKind::Short:
  case janus::TypeKind::UShort:
  case janus::TypeKind::ISize:
  case janus::TypeKind::Char:
  case janus::TypeKind::Bool:
  case janus::TypeKind::USize:
    return true;
  case janus::TypeKind::String:
  case janus::TypeKind::Unit:
  case janus::TypeKind::Enum:
  case janus::TypeKind::Function:
  case janus::TypeKind::Pointer:
  case janus::TypeKind::Class:
  case janus::TypeKind::Struct:
    return false;
  }
  return false;
}

bool can_explicitly_cast(const janus::semantic::SemanticType &source,
                         const janus::semantic::SemanticType &destination) {
  if (same_type(source, destination))
    return true;
  if (is_scalar_cast_type(source) && is_scalar_cast_type(destination))
    return true;

  const bool source_is_reference = source.is_pointer() || source.is_class();
  const bool destination_is_reference =
      destination.is_pointer() || destination.is_class();
  if (source_is_reference && destination_is_reference)
    return true;

  return (source_is_reference && is_integer_cast_type(destination)) ||
         (is_integer_cast_type(source) && destination_is_reference);
}

janus::semantic::SemanticType
substitute(janus::semantic::SemanticType type,
           const std::unordered_map<std::string, janus::semantic::SemanticType>
               &substitutions) {
  if (!type.is_concrete() && !type.is_class()) {
    if (const auto iterator = substitutions.find(type.parameter);
        iterator != substitutions.end()) {
      return iterator->second;
    }
  }
  for (janus::semantic::SemanticType &argument : type.type_arguments)
    argument = substitute(std::move(argument), substitutions);
  return type;
}

janus::SourceLocation expression_location(const janus::ast::Expression &expr) {
  return std::visit([](const auto &node) { return node.location; }, expr.value);
}

bool block_guarantees_return(const std::vector<janus::ast::Statement> &block);

bool statement_guarantees_return(const janus::ast::Statement &statement) {
  if (std::holds_alternative<janus::ast::ReturnStatement>(statement))
    return true;
  if (const auto *expression =
          std::get_if<janus::ast::ExpressionStatement>(&statement)) {
    if (const auto *call = std::get_if<janus::ast::CallExpression>(
            &expression->expression.value))
      return call->callee == "panic";
  }

  if (const auto *conditional =
          std::get_if<std::shared_ptr<janus::ast::IfStatement>>(&statement)) {
    return !(*conditional)->else_body.empty() &&
           block_guarantees_return((*conditional)->then_body) &&
           block_guarantees_return((*conditional)->else_body);
  }

  // Loops are intentionally conservative: reaching the loop does not prove
  // that an iteration runs or that control cannot leave through break.
  return false;
}

bool block_guarantees_return(const std::vector<janus::ast::Statement> &block) {
  return std::any_of(block.begin(), block.end(), statement_guarantees_return);
}

std::optional<__int128>
integer_literal_value(const janus::ast::Expression &expression) {
  if (const auto *literal = std::get_if<janus::ast::IntegerLiteralExpression>(
          &expression.value)) {
    const __int128 magnitude = static_cast<__int128>(literal->magnitude);
    return literal->is_negative ? -magnitude : magnitude;
  }
  return std::nullopt;
}

bool integer_literal_fits(const janus::ast::Expression &expression,
                          const janus::Type &type) {
  const auto value = integer_literal_value(expression);
  if (!value || !type.is_integer())
    return false;
  if (type.is_signed()) {
    const std::uint32_t magnitude_bits = type.bit_width() - 1;
    const __int128 minimum = -(__int128{1} << magnitude_bits);
    const __int128 maximum = (__int128{1} << magnitude_bits) - 1;
    return *value >= minimum && *value <= maximum;
  }
  const unsigned __int128 maximum =
      type.bit_width() == 64
          ? std::numeric_limits<std::uint64_t>::max()
          : (static_cast<unsigned __int128>(1) << type.bit_width()) - 1;
  return *value >= 0 && static_cast<unsigned __int128>(*value) <= maximum;
}

bool accepts_contextual_integer_literal(const janus::Type &type) {
  return type.is_integer();
}

std::string integer_range_description(const janus::Type &type) {
  return std::string{type.is_signed() ? "signed " : "unsigned "} +
         std::to_string(type.bit_width()) + "-bit range";
}

std::string global_key(const std::optional<std::string> &module,
                       std::string_view name) {
  return module.has_value() ? *module + "." + std::string{name}
                            : std::string{name};
}

std::optional<std::string>
qualified_expression_name(const janus::ast::Expression &expression) {
  if (const auto *identifier =
          std::get_if<janus::ast::IdentifierExpression>(&expression.value))
    return identifier->name;
  if (const auto *member =
          std::get_if<janus::ast::MemberAccessExpression>(&expression.value)) {
    if (auto prefix = qualified_expression_name(*member->object))
      return *prefix + "." + member->member;
  }
  return std::nullopt;
}

} // namespace

namespace janus::semantic {

std::string SemanticType::name() const {
  if (is_concrete())
    return std::string{concrete->name()};
  if (is_function()) {
    std::string result{"("};
    for (std::size_t index = 0; index + 1 < type_arguments.size(); ++index) {
      if (index != 0)
        result += ", ";
      result += type_arguments[index].name();
    }
    result += ") => ";
    result += type_arguments.back().name();
    return result;
  }
  std::string result = parameter;
  if (!type_arguments.empty()) {
    result += '[';
    for (std::size_t index = 0; index < type_arguments.size(); ++index) {
      if (index != 0)
        result += ", ";
      result += type_arguments[index].name();
    }
    result += ']';
  }
  return result;
}

AnalysisResult Analyzer::analyze(const ast::Program &program,
                                 AnalysisOptions options) const {
  AnalysisResult result;
  options.target.validate();
  result.target = options.target;
  std::unordered_map<std::string, const ast::FunctionDeclaration *> functions;
  std::unordered_map<std::string, const ast::ClassDeclaration *> classes;
  std::unordered_map<std::string, const ast::EnumDeclaration *> enums;
  std::unordered_map<std::string, const ast::TraitDeclaration *> traits;
  std::unordered_map<std::string, std::size_t> class_arities;
  std::unordered_map<std::string, std::size_t> type_name_counts;
  std::unordered_set<std::string> type_identities;
  std::unordered_set<std::string> scoped_type_aliases;
  const auto imported_names = [&](const std::optional<std::string> &module,
                                  std::string_view name) {
    std::vector<std::string> names;
    if (!module.has_value())
      return names;
    for (const ast::ImportDeclaration &import : program.imports) {
      if (import.module_name != *module)
        continue;
      for (const ast::ImportDeclaration::Symbol &symbol : import.symbols)
        if (symbol.name == name)
          names.push_back(global_key(import.importing_module,
                                     symbol.alias.value_or(symbol.name)));
      if (import.module_alias.has_value())
        names.push_back(
            global_key(import.importing_module,
                       *import.module_alias + "." + std::string{name}));
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
  };
  const auto find_in_context = [&](auto &symbols,
                                   const std::optional<std::string> &module,
                                   std::string_view name) {
    auto iterator = symbols.find(global_key(module, name));
    if (iterator == symbols.end())
      iterator = symbols.find(std::string{name});
    return iterator;
  };
  std::unordered_map<std::string, std::vector<std::string>> legacy_import_edges;
  for (const ast::ImportDeclaration &import : program.imports)
    if (!import.is_qualified() && !import.is_selective())
      legacy_import_edges[import.importing_module.value_or(std::string{})]
          .push_back(import.module_name);
  std::unordered_map<std::string, std::unordered_set<std::string>>
      legacy_import_closure;
  for (const auto &[importer, direct_imports] : legacy_import_edges) {
    std::vector<std::string> pending = direct_imports;
    std::unordered_set<std::string> &reachable =
        legacy_import_closure[importer];
    while (!pending.empty()) {
      std::string dependency = std::move(pending.back());
      pending.pop_back();
      if (!reachable.insert(dependency).second)
        continue;
      if (const auto nested = legacy_import_edges.find(dependency);
          nested != legacy_import_edges.end())
        pending.insert(pending.end(), nested->second.begin(),
                       nested->second.end());
    }
  }
  const auto import_allows =
      [&](const std::optional<std::string> &importer,
          const std::optional<std::string> &declaring_module,
          std::string_view canonical_name, std::string_view spelling) {
        if (importer == declaring_module || !declaring_module.has_value())
          return true;
        for (const ast::ImportDeclaration &import : program.imports) {
          if (import.importing_module != importer ||
              import.module_name != *declaring_module)
            continue;
          if (!import.is_qualified() && !import.is_selective() &&
              spelling == canonical_name)
            return true;
          if (import.module_alias.has_value() &&
              spelling ==
                  *import.module_alias + "." + std::string{canonical_name})
            return true;
          if (!import.is_qualified() && !import.is_selective() &&
              spelling ==
                  import.module_name + "." + std::string{canonical_name})
            return true;
          for (const ast::ImportDeclaration::Symbol &symbol : import.symbols)
            if (symbol.name == canonical_name &&
                spelling == symbol.alias.value_or(symbol.name))
              return true;
        }
        // Plain imports intentionally re-export their plain dependencies (for
        // example std.graphics). Limit that legacy behavior to the dependency
        // closure rooted at the current module so a sibling branch cannot
        // widen lexical visibility.
        if (spelling == canonical_name) {
          const auto reachable =
              legacy_import_closure.find(importer.value_or(std::string{}));
          if (reachable != legacy_import_closure.end() &&
              reachable->second.contains(*declaring_module))
            return true;
        }
        return false;
      };
  const auto register_type_identity =
      [&](const std::optional<std::string> &module, std::string_view name,
          SourceLocation location) {
        const std::string identity = global_key(module, name);
        if (!type_identities.insert(identity).second)
          throw CompileError{location,
                             "type '" + identity + "' is already declared"};
        ++type_name_counts[std::string{name}];
        return identity;
      };
  for (const ast::EnumDeclaration &declaration : program.enums) {
    const std::string identity = register_type_identity(
        declaration.module_name, declaration.name, declaration.location);
    enums.emplace(identity, &declaration);
    class_arities.emplace(identity, enum_arity_marker +
                                        declaration.type_parameters.size());
    for (const std::string &alias :
         imported_names(declaration.module_name, declaration.name)) {
      scoped_type_aliases.insert(alias);
      enums.emplace(alias, &declaration);
      class_arities.emplace(alias, enum_arity_marker +
                                       declaration.type_parameters.size());
    }
  }
  for (const ast::TraitDeclaration &declaration : program.traits) {
    const std::string identity = register_type_identity(
        declaration.module_name, declaration.name, declaration.location);
    traits.emplace(identity, &declaration);
    for (const std::string &alias :
         imported_names(declaration.module_name, declaration.name)) {
      scoped_type_aliases.insert(alias);
      traits.emplace(alias, &declaration);
    }
  }
  for (const ast::ClassDeclaration &declaration : program.classes) {
    const std::string identity = register_type_identity(
        declaration.module_name, declaration.name, declaration.location);
    classes.emplace(identity, &declaration);
    class_arities.emplace(identity, declaration.type_parameters.size());
    for (const std::string &alias :
         imported_names(declaration.module_name, declaration.name)) {
      scoped_type_aliases.insert(alias);
      classes.emplace(alias, &declaration);
      class_arities.emplace(alias, declaration.type_parameters.size());
    }
  }
  for (const ast::EnumDeclaration &declaration : program.enums)
    if (type_name_counts.at(declaration.name) == 1) {
      enums.emplace(declaration.name, &declaration);
      class_arities.emplace(declaration.name,
                            enum_arity_marker +
                                declaration.type_parameters.size());
    } else {
      class_arities.insert_or_assign(declaration.name, ambiguous_arity_marker);
    }
  for (const ast::TraitDeclaration &declaration : program.traits)
    if (type_name_counts.at(declaration.name) == 1)
      traits.emplace(declaration.name, &declaration);
  for (const ast::ClassDeclaration &declaration : program.classes)
    if (type_name_counts.at(declaration.name) == 1) {
      classes.emplace(declaration.name, &declaration);
      class_arities.emplace(declaration.name,
                            declaration.type_parameters.size());
    } else {
      class_arities.insert_or_assign(declaration.name, ambiguous_arity_marker);
    }

  struct TypeVisibility {
    bool is_private;
    std::optional<std::string> module;
    std::string name;
  };
  std::unordered_map<std::string, TypeVisibility> type_visibility;
  for (const ast::EnumDeclaration &declaration : program.enums) {
    const TypeVisibility visibility{declaration.is_private,
                                    declaration.module_name, declaration.name};
    type_visibility.emplace(
        global_key(declaration.module_name, declaration.name), visibility);
    for (const std::string &alias :
         imported_names(declaration.module_name, declaration.name))
      type_visibility.emplace(alias, visibility);
  }
  for (const ast::TraitDeclaration &declaration : program.traits) {
    const TypeVisibility visibility{declaration.is_private,
                                    declaration.module_name, declaration.name};
    type_visibility.emplace(
        global_key(declaration.module_name, declaration.name), visibility);
    for (const std::string &alias :
         imported_names(declaration.module_name, declaration.name))
      type_visibility.emplace(alias, visibility);
  }
  for (const ast::ClassDeclaration &declaration : program.classes) {
    const TypeVisibility visibility{declaration.is_private,
                                    declaration.module_name, declaration.name};
    type_visibility.emplace(
        global_key(declaration.module_name, declaration.name), visibility);
    for (const std::string &alias :
         imported_names(declaration.module_name, declaration.name))
      type_visibility.emplace(alias, visibility);
  }
  const auto check_type_visibility =
      [&](const auto &self, const ast::TypeReference &reference,
          const std::optional<std::string> &context_module) -> void {
    std::string identity = reference.name;
    if (reference.name.find('.') == std::string::npos) {
      const std::string local = global_key(context_module, reference.name);
      if (type_visibility.contains(local))
        identity = local;
      else if (type_name_counts.contains(reference.name) &&
               type_name_counts.at(reference.name) == 1)
        for (const auto &[candidate, visibility] : type_visibility)
          if (candidate == reference.name ||
              candidate.ends_with("." + reference.name)) {
            identity = candidate;
            break;
          }
    }
    if (const auto visibility = type_visibility.find(identity);
        visibility != type_visibility.end()) {
      if (visibility->second.is_private &&
          visibility->second.module != context_module)
        throw CompileError{reference.location,
                           "type '" + identity + "' is private"};
      if (!import_allows(context_module, visibility->second.module,
                         visibility->second.name, reference.name))
        throw CompileError{reference.location,
                           "type '" + reference.name +
                               "' is not imported in this module"};
    }
    for (const ast::TypeReference &argument : reference.type_arguments)
      self(self, argument, context_module);
  };
  const auto check_function_signature_visibility =
      [&](const ast::FunctionDeclaration &function,
          const std::optional<std::string> &context_module) {
        check_type_visibility(check_type_visibility, function.return_type,
                              context_module);
        for (const ast::FunctionDeclaration::Parameter &parameter :
             function.parameters)
          check_type_visibility(check_type_visibility, parameter.type,
                                context_module);
      };
  for (const ast::FunctionDeclaration &function : program.functions)
    check_function_signature_visibility(function, function.module_name);
  for (const ast::GlobalDeclaration &global : program.globals)
    check_type_visibility(check_type_visibility,
                          *global.declaration.declared_type,
                          global.module_name);
  for (const ast::EnumDeclaration &declaration : program.enums)
    for (const ast::EnumDeclaration::Case &enum_case : declaration.cases)
      for (const ast::TypeReference &payload : enum_case.payload_types)
        check_type_visibility(check_type_visibility, payload,
                              declaration.module_name);
  for (const ast::TraitDeclaration &declaration : program.traits)
    for (const ast::FunctionDeclaration &method : declaration.methods)
      check_function_signature_visibility(method, declaration.module_name);
  for (const ast::ClassDeclaration &declaration : program.classes) {
    for (const ast::TypeReference &implemented : declaration.implemented_traits)
      check_type_visibility(check_type_visibility, implemented,
                            declaration.module_name);
    for (const ast::FunctionDeclaration::Parameter &parameter :
         declaration.constructor_parameters)
      check_type_visibility(check_type_visibility, parameter.type,
                            declaration.module_name);
    for (const ast::ValueDeclaration &field : declaration.constructor_fields)
      check_type_visibility(check_type_visibility, *field.declared_type,
                            declaration.module_name);
    for (const ast::ValueDeclaration &field : declaration.fields)
      check_type_visibility(check_type_visibility, *field.declared_type,
                            declaration.module_name);
    for (const ast::FunctionDeclaration &method : declaration.methods)
      check_function_signature_visibility(method, declaration.module_name);
  }

  for (const ast::EnumDeclaration &enum_declaration : program.enums) {
    if (builtin_type(enum_declaration.name) != nullptr ||
        enum_declaration.name == "Function")
      throw CompileError{enum_declaration.location,
                         "enum '" + enum_declaration.name +
                             "' conflicts with a built-in type"};
    std::unordered_set<std::string> enum_parameters;
    for (const std::string &parameter : enum_declaration.type_parameters) {
      if (!enum_parameters.insert(parameter).second)
        throw CompileError{enum_declaration.location,
                           "type parameter '" + parameter +
                               "' is already declared"};
      if (builtin_type(parameter) != nullptr || parameter == "Function")
        throw CompileError{enum_declaration.location,
                           "type parameter '" + parameter +
                               "' conflicts with a built-in type"};
    }
    std::unordered_set<std::string> case_names;
    for (const ast::EnumDeclaration::Case &enum_case : enum_declaration.cases) {
      if (!case_names.insert(enum_case.name).second)
        throw CompileError{enum_case.location,
                           "enum case '" + enum_case.name +
                               "' is already declared in enum '" +
                               enum_declaration.name + "'"};
    }
  }
  for (const ast::TraitDeclaration &trait_declaration : program.traits) {
    if (trait_declaration.name == "Copy")
      throw CompileError{trait_declaration.location,
                         "trait 'Copy' is intrinsic and cannot be redeclared"};
    if (builtin_type(trait_declaration.name) != nullptr ||
        trait_declaration.name == "Function")
      throw CompileError{trait_declaration.location,
                         "trait '" + trait_declaration.name +
                             "' conflicts with a built-in type"};
  }
  for (const ast::ClassDeclaration &class_declaration : program.classes) {
    if (builtin_type(class_declaration.name) != nullptr ||
        class_declaration.name == "Function")
      throw CompileError{class_declaration.location,
                         "class '" + class_declaration.name +
                             "' conflicts with a built-in type"};
  }

  struct ResolvedGlobal {
    const ast::GlobalDeclaration *declaration;
    Symbol symbol;
  };
  std::unordered_map<std::string, ResolvedGlobal> globals;
  std::unordered_map<std::string, std::string> public_globals;
  std::unordered_set<std::string> global_modules;
  const std::unordered_set<std::string> no_type_parameters;
  const auto directly_owns_value = [&](const SemanticType &type) {
    if (type.is_function() || type.is_pointer())
      return true;
    if (type.is_class()) {
      const ast::ClassDeclaration &declaration = *classes.at(type.parameter);
      if (!declaration.is_value_type)
        return true;
    }
    return false;
  };
  const auto visit_owned_children = [&](const SemanticType &type,
                                        const auto &visit) {
    if (type.is_class()) {
      const ast::ClassDeclaration &declaration = *classes.at(type.parameter);
      if (!declaration.is_value_type)
        return;
      const std::unordered_set<std::string> parameters{
          declaration.type_parameters.begin(),
          declaration.type_parameters.end()};
      std::unordered_map<std::string, SemanticType> substitutions;
      for (std::size_t index = 0; index < declaration.type_parameters.size();
           ++index)
        substitutions.emplace(declaration.type_parameters[index],
                              type.type_arguments[index]);
      for (const ast::ValueDeclaration &field :
           declaration.constructor_fields) {
        SemanticType field_type =
            resolve_type(*field.declared_type, parameters, &class_arities);
        visit(substitute(std::move(field_type), substitutions));
      }
      return;
    }
    if (type.is_enum()) {
      const ast::EnumDeclaration &declaration = *enums.at(type.parameter);
      const std::unordered_set<std::string> parameters{
          declaration.type_parameters.begin(),
          declaration.type_parameters.end()};
      std::unordered_map<std::string, SemanticType> substitutions;
      for (std::size_t index = 0; index < declaration.type_parameters.size();
           ++index)
        substitutions.emplace(declaration.type_parameters[index],
                              type.type_arguments[index]);
      for (const ast::EnumDeclaration::Case &enum_case : declaration.cases)
        for (const ast::TypeReference &payload : enum_case.payload_types) {
          SemanticType payload_type =
              resolve_type(payload, parameters, &class_arities);
          visit(substitute(std::move(payload_type), substitutions));
        }
    }
  };
  const auto aggregate_owns_value = [&](const SemanticType &type) {
    return janus::ownership::recursively_owns_value(type, directly_owns_value,
                                                    visit_owned_children);
  };

  const auto validate_derivation_dependencies = [&](const auto &declaration) {
    if (has_derivation(declaration.derivations, ast::DerivationKind::Hashing) &&
        !has_derivation(declaration.derivations,
                        ast::DerivationKind::Equality)) {
      const auto hashing = std::find_if(
          declaration.derivations.begin(), declaration.derivations.end(),
          [](const ast::Derivation &item) {
            return item.kind == ast::DerivationKind::Hashing;
          });
      throw CompileError{hashing->location,
                         "deriving Hashing requires Equality on type '" +
                             declaration.name + "'"};
    }
  };
  for (const ast::EnumDeclaration &declaration : program.enums)
    validate_derivation_dependencies(declaration);
  for (const ast::ClassDeclaration &declaration : program.classes) {
    validate_derivation_dependencies(declaration);
    if (!declaration.is_value_type &&
        has_derivation(declaration.derivations, ast::DerivationKind::Copy)) {
      const auto copy = std::find_if(
          declaration.derivations.begin(), declaration.derivations.end(),
          [](const ast::Derivation &item) {
            return item.kind == ast::DerivationKind::Copy;
          });
      throw CompileError{copy->location, "cannot derive Copy for class '" +
                                             declaration.name + "'"};
    }
    if (declaration.is_value_type &&
        has_derivation(declaration.derivations, ast::DerivationKind::Copy)) {
      const std::unordered_set<std::string> parameters{
          declaration.type_parameters.begin(),
          declaration.type_parameters.end()};
      for (const ast::ValueDeclaration &field :
           declaration.constructor_fields) {
        const SemanticType field_type =
            resolve_type(*field.declared_type, parameters, &class_arities);
        if (aggregate_owns_value(field_type)) {
          const auto copy = std::find_if(
              declaration.derivations.begin(), declaration.derivations.end(),
              [](const ast::Derivation &item) {
                return item.kind == ast::DerivationKind::Copy;
              });
          throw CompileError{
              copy->location,
              "cannot derive Copy for '" + declaration.name + "': field '" +
                  field.name + "' has owning type '" + field_type.name() + "'"};
        }
      }
    }
  }
  for (const ast::ClassDeclaration &declaration : program.classes) {
    if (declaration.is_value_type || declaration.destructor.has_value() ||
        declaration.module_name != program.module_name)
      continue;
    const std::unordered_set<std::string> parameters{
        declaration.type_parameters.begin(), declaration.type_parameters.end()};
    const auto check_field = [&](const ast::ValueDeclaration &field) {
      if (field.is_borrowed)
        return;
      const SemanticType field_type =
          resolve_type(*field.declared_type, parameters, &class_arities);
      if (!aggregate_owns_value(field_type))
        return;
      result.diagnostics.push_back(Diagnostic{
          DiagnosticSeverity::Warning,
          DiagnosticCode::AnalyzerIncompleteDestructor,
          "class '" + declaration.name +
              "' has no destructor for owning "
              "field '" +
              field.name + "' of type '" + field_type.name() + "'",
          field.location,
          {"declare a destructor that deletes, frees, or moves the field"},
          {},
          {},
      });
    };
    for (const ast::ValueDeclaration &field : declaration.constructor_fields)
      check_field(field);
    for (const ast::ValueDeclaration &field : declaration.fields)
      check_field(field);
  }
  using ClassPointer = const ast::ClassDeclaration *;
  std::unordered_map<ClassPointer, std::vector<ClassPointer>> ownership_edges;
  const auto collect_owned_classes =
      [&](const auto &self, const SemanticType &type,
          std::vector<ClassPointer> &targets,
          std::unordered_set<std::string> &visiting) -> void {
    if (type.is_class()) {
      const ast::ClassDeclaration &nested = *classes.at(type.parameter);
      if (!nested.is_value_type) {
        targets.push_back(&nested);
        return;
      }
      if (!visiting.insert(type.name()).second)
        return;
      const std::unordered_set<std::string> parameters{
          nested.type_parameters.begin(), nested.type_parameters.end()};
      std::unordered_map<std::string, SemanticType> substitutions;
      for (std::size_t index = 0; index < nested.type_parameters.size();
           ++index)
        substitutions.emplace(nested.type_parameters[index],
                              type.type_arguments[index]);
      for (const ast::ValueDeclaration &field : nested.constructor_fields) {
        if (field.is_borrowed)
          continue;
        SemanticType field_type =
            resolve_type(*field.declared_type, parameters, &class_arities);
        self(self, substitute(std::move(field_type), substitutions), targets,
             visiting);
      }
      visiting.erase(type.name());
      return;
    }
    if (!type.is_enum())
      return;
    if (!visiting.insert(type.name()).second)
      return;
    const ast::EnumDeclaration &nested = *enums.at(type.parameter);
    const std::unordered_set<std::string> parameters{
        nested.type_parameters.begin(), nested.type_parameters.end()};
    std::unordered_map<std::string, SemanticType> substitutions;
    for (std::size_t index = 0; index < nested.type_parameters.size(); ++index)
      substitutions.emplace(nested.type_parameters[index],
                            type.type_arguments[index]);
    for (const ast::EnumDeclaration::Case &enum_case : nested.cases)
      for (const ast::TypeReference &payload : enum_case.payload_types) {
        SemanticType payload_type =
            resolve_type(payload, parameters, &class_arities);
        self(self, substitute(std::move(payload_type), substitutions), targets,
             visiting);
      }
    visiting.erase(type.name());
  };
  for (const ast::ClassDeclaration &declaration : program.classes) {
    std::vector<ClassPointer> &targets = ownership_edges[&declaration];
    const std::unordered_set<std::string> parameters{
        declaration.type_parameters.begin(), declaration.type_parameters.end()};
    const auto inspect_field = [&](const ast::ValueDeclaration &field) {
      if (field.is_borrowed)
        return;
      const SemanticType field_type =
          resolve_type(*field.declared_type, parameters, &class_arities);
      std::unordered_set<std::string> visiting;
      collect_owned_classes(collect_owned_classes, field_type, targets,
                            visiting);
    };
    for (const ast::ValueDeclaration &field : declaration.constructor_fields)
      inspect_field(field);
    for (const ast::ValueDeclaration &field : declaration.fields)
      inspect_field(field);
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  }
  const auto class_identity = [](ClassPointer declaration) {
    return declaration->module_name.value_or("<entry>") + "." +
           declaration->name;
  };
  std::unordered_set<std::string> reported_ownership_cycles;
  for (const ast::ClassDeclaration &root_declaration : program.classes) {
    if (root_declaration.module_name != program.module_name)
      continue;
    const ClassPointer root = &root_declaration;
    std::vector<ClassPointer> path{root};
    std::unordered_set<ClassPointer> active_path{root};
    const auto find_cycles = [&](const auto &self,
                                 ClassPointer current) -> void {
      for (ClassPointer target : ownership_edges[current]) {
        if (target == root) {
          std::vector<std::string> identities;
          identities.reserve(path.size());
          for (ClassPointer member : path)
            identities.push_back(class_identity(member));
          std::sort(identities.begin(), identities.end());
          identities.erase(std::unique(identities.begin(), identities.end()),
                           identities.end());
          std::string signature;
          for (const std::string &identity : identities)
            signature += identity + "|";
          if (!reported_ownership_cycles.insert(signature).second)
            continue;
          std::string description;
          for (std::size_t index = 0; index < path.size(); ++index) {
            if (index != 0)
              description += " -> ";
            description += path[index]->name;
          }
          description += " -> " + root->name;
          result.diagnostics.push_back(Diagnostic{
              DiagnosticSeverity::Warning,
              DiagnosticCode::AnalyzerPotentialOwnershipCycle,
              "owning fields form a potential cycle: " + description,
              root->location,
              {"break the cycle with a borrowed reference or explicitly "
               "clear one owning edge before destruction"},
              {},
              {},
          });
          continue;
        }
        if (!active_path.insert(target).second)
          continue;
        path.push_back(target);
        self(self, target);
        path.pop_back();
        active_path.erase(target);
      }
    };
    find_cycles(find_cycles, root);
  }
  const auto validate_structural_fields =
      [&](const auto &declaration, ast::DerivationKind kind,
          const std::vector<ast::ValueDeclaration> &fields) {
        if (!has_derivation(declaration.derivations, kind))
          return;
        const std::unordered_set<std::string> parameters{
            declaration.type_parameters.begin(),
            declaration.type_parameters.end()};
        for (const ast::ValueDeclaration &field : fields) {
          const ast::TypeReference &type = *field.declared_type;
          bool supported = builtin_type(type.name) != nullptr ||
                           parameters.contains(type.name);
          if (type.name == "Ptr" || type.name == "Function")
            supported = false;
          if (const auto nested = classes.find(type.name);
              nested != classes.end())
            supported = has_derivation(nested->second->derivations, kind);
          if (const auto nested = enums.find(type.name); nested != enums.end())
            supported = has_derivation(nested->second->derivations, kind);
          if (!supported) {
            const auto capability = std::find_if(
                declaration.derivations.begin(), declaration.derivations.end(),
                [kind](const ast::Derivation &item) {
                  return item.kind == kind;
                });
            throw CompileError{
                capability->location,
                "cannot derive " + std::string{derivation_name(kind)} +
                    " for '" + declaration.name + "': field '" + field.name +
                    "' of type '" + type.name + "' does not support " +
                    std::string{derivation_name(kind)}};
          }
        }
      };
  for (const ast::ClassDeclaration &declaration : program.classes) {
    for (const ast::DerivationKind kind :
         {ast::DerivationKind::Copy, ast::DerivationKind::Equality,
          ast::DerivationKind::Hashing, ast::DerivationKind::Debug}) {
      validate_structural_fields(declaration, kind,
                                 declaration.constructor_fields);
      validate_structural_fields(declaration, kind, declaration.fields);
    }
  }
  for (const ast::EnumDeclaration &declaration : program.enums) {
    const std::unordered_set<std::string> parameters{
        declaration.type_parameters.begin(), declaration.type_parameters.end()};
    for (const ast::DerivationKind kind :
         {ast::DerivationKind::Copy, ast::DerivationKind::Equality,
          ast::DerivationKind::Hashing, ast::DerivationKind::Debug}) {
      if (!has_derivation(declaration.derivations, kind))
        continue;
      for (const ast::EnumDeclaration::Case &enum_case : declaration.cases)
        for (std::size_t index = 0; index < enum_case.payload_types.size();
             ++index) {
          const ast::TypeReference &type = enum_case.payload_types[index];
          bool supported = builtin_type(type.name) != nullptr ||
                           parameters.contains(type.name);
          if (type.name == "Ptr" || type.name == "Function")
            supported = false;
          if (const auto nested = classes.find(type.name);
              nested != classes.end())
            supported =
                kind == ast::DerivationKind::Copy
                    ? nested->second->is_value_type &&
                          has_derivation(nested->second->derivations, kind)
                    : has_derivation(nested->second->derivations, kind);
          if (const auto nested = enums.find(type.name); nested != enums.end())
            supported = has_derivation(nested->second->derivations, kind);
          if (!supported) {
            const auto capability = std::find_if(
                declaration.derivations.begin(), declaration.derivations.end(),
                [kind](const ast::Derivation &item) {
                  return item.kind == kind;
                });
            throw CompileError{
                capability->location,
                "cannot derive " + std::string{derivation_name(kind)} +
                    " for '" + declaration.name + "': case '" + enum_case.name +
                    "' payload " + std::to_string(index + 1) + " of type '" +
                    type.name + "' does not support " +
                    std::string{derivation_name(kind)}};
          }
        }
    }
  }
  for (const ast::GlobalDeclaration &global : program.globals) {
    const ast::ValueDeclaration &declaration = global.declaration;
    const std::string key = global_key(global.module_name, declaration.name);
    if (globals.contains(key))
      throw CompileError{declaration.location,
                         "global value '" + key + "' is already declared"};
    if (!declaration.is_private && !declaration.is_internal) {
      if (const auto existing = public_globals.find(declaration.name);
          existing != public_globals.end())
        throw CompileError{declaration.location,
                           "public global value '" + declaration.name +
                               "' is exported by both modules '" +
                               existing->second + "' and '" +
                               global.module_name.value_or("<entry>") + "'"};
      public_globals.emplace(declaration.name, key);
    }
    if (global.module_name.has_value())
      global_modules.insert(*global.module_name);
    SemanticType type = resolve_type(*declaration.declared_type,
                                     no_type_parameters, &class_arities);
    if (declaration.declared_type->name == "isize")
      type.concrete = &Type::isize_type(options.target.pointer_width);
    else if (declaration.declared_type->name == "usize")
      type.concrete = &Type::usize_type(options.target.pointer_width);
    if (type.is_concrete() && type.concrete->kind() == TypeKind::Unit)
      throw CompileError{declaration.location,
                         "Unit cannot be used as a global value type"};
    const bool owns_value = aggregate_owns_value(type);
    if (owns_value && declaration.is_mutable)
      throw CompileError{declaration.location,
                         "owning global value '" + declaration.name +
                             "' must be declared with 'val'"};
    if (!declaration.initializer.has_value())
      throw CompileError{declaration.location, "global variable '" +
                                                   declaration.name +
                                                   "' requires an initializer"};
    Symbol symbol{type, declaration.is_mutable, true};
    result.globals.emplace(key, symbol);
    globals.emplace(key, ResolvedGlobal{&global, symbol});
    for (const std::string &alias :
         imported_names(global.module_name, declaration.name)) {
      if (!declaration.is_private && !declaration.is_internal)
        public_globals.insert_or_assign(alias, key);
      globals.emplace(alias, ResolvedGlobal{&global, symbol});
      if (const std::size_t dot = alias.rfind('.'); dot != std::string::npos)
        global_modules.insert(alias.substr(0, dot));
    }
  }

  enum class ConstantState { Unvisited, Visiting, Complete };
  std::unordered_map<std::string, ConstantState> constant_states;
  std::unordered_map<std::string, constant::Value> constant_values;
  std::unordered_map<std::string, const Type *> constant_nominal_types;
  std::unordered_map<std::string, constant::ConstructorShape>
      constant_constructor_shapes;

  const auto constant_constructor_resolver =
      [&](std::string_view name, const std::optional<std::string> &enum_case,
          const std::vector<ast::TypeReference> &type_references,
          SourceLocation location)
      -> std::optional<constant::ConstructorShape> {
    const std::string shape_key =
        std::string{name} + (enum_case ? "." + *enum_case : "");
    if (type_references.empty())
      if (const auto cached = constant_constructor_shapes.find(shape_key);
          cached != constant_constructor_shapes.end())
        return cached->second;

    const auto concrete_type =
        [&](const ast::TypeReference &reference,
            const std::unordered_map<std::string, const Type *> &substitutions)
        -> const Type * {
      if (const auto replacement = substitutions.find(reference.name);
          replacement != substitutions.end() &&
          reference.type_arguments.empty())
        return replacement->second;
      return builtin_type(reference.name);
    };
    if (enum_case.has_value()) {
      const auto declaration = enums.find(std::string{name});
      if (declaration == enums.end())
        return std::nullopt;
      const auto matched = std::find_if(
          declaration->second->cases.begin(), declaration->second->cases.end(),
          [&](const ast::EnumDeclaration::Case &candidate) {
            return candidate.name == *enum_case;
          });
      if (matched == declaration->second->cases.end())
        return std::nullopt;
      if (type_references.size() != declaration->second->type_parameters.size())
        return std::nullopt;
      std::unordered_map<std::string, const Type *> substitutions;
      for (std::size_t index = 0; index < type_references.size(); ++index) {
        const Type *argument = builtin_type(type_references[index].name);
        if (argument == nullptr)
          return std::nullopt;
        substitutions.emplace(declaration->second->type_parameters[index],
                              argument);
      }
      const Type *nominal = nullptr;
      if (const auto existing = constant_nominal_types.find(std::string{name});
          existing != constant_nominal_types.end()) {
        nominal = existing->second;
      } else {
        auto owned = std::make_shared<Type>(Type::enum_type(name));
        nominal = owned.get();
        result.constant_value_types.push_back(std::move(owned));
        constant_nominal_types.emplace(name, nominal);
      }
      constant::ConstructorShape shape{nominal, matched->value, {}};
      for (std::size_t index = 0; index < matched->payload_types.size();
           ++index) {
        const Type *payload =
            concrete_type(matched->payload_types[index], substitutions);
        if (payload == nullptr)
          throw CompileError{location,
                             "constant enum payload type is not admissible"};
        shape.fields.emplace_back(index + 1, payload);
      }
      constant_constructor_shapes.insert_or_assign(shape_key, shape);
      return shape;
    }
    const auto declaration = classes.find(std::string{name});
    if (declaration == classes.end() || !declaration->second->is_value_type ||
        !declaration->second->type_parameters.empty())
      return std::nullopt;
    const Type *nominal = nullptr;
    if (const auto existing = constant_nominal_types.find(std::string{name});
        existing != constant_nominal_types.end()) {
      nominal = existing->second;
    } else {
      auto owned = std::make_shared<Type>(Type::struct_type(name));
      nominal = owned.get();
      result.constant_value_types.push_back(std::move(owned));
      constant_nominal_types.emplace(name, nominal);
    }
    constant::ConstructorShape shape{nominal, std::nullopt, {}};
    for (std::size_t index = 0;
         index < declaration->second->constructor_fields.size(); ++index) {
      const Type *field = builtin_type(
          declaration->second->constructor_fields[index].declared_type->name);
      if (field == nullptr)
        throw CompileError{location,
                           "constant struct field type is not admissible"};
      shape.fields.emplace_back(index, field);
    }
    constant_constructor_shapes.insert_or_assign(shape_key, shape);
    return shape;
  };

  // A const function is a declaration-time purity contract.  Validate every
  // body, even when no constant initializer happens to call it.
  enum class PurityState { Unvisited, Visiting, Complete };
  std::unordered_map<const ast::FunctionDeclaration *, PurityState>
      purity_states;
  const auto is_constant_builtin = [](std::string_view name) {
    static constexpr std::array<std::string_view, 17> names{
        "int",   "uint",  "long",           "ulong",          "float", "double",
        "byte",  "ubyte", "short",          "ushort",         "char",  "bool",
        "isize", "usize", "saturatingCast", "truncatingCast", "abs"};
    return std::find(names.begin(), names.end(), name) != names.end();
  };
  const auto find_constant_function =
      [&](std::string_view name) -> const ast::FunctionDeclaration * {
    const auto found = std::find_if(
        program.functions.begin(), program.functions.end(),
        [&](const ast::FunctionDeclaration &candidate) {
          if (!candidate.is_constant)
            return false;
          if (candidate.name == name)
            return !candidate.module_name.has_value() ||
                   import_allows(program.module_name, candidate.module_name,
                                 candidate.name, name);
          const auto aliases =
              imported_names(candidate.module_name, candidate.name);
          return std::find(aliases.begin(), aliases.end(), name) !=
                 aliases.end();
        });
    return found == program.functions.end() ? nullptr : &*found;
  };
  std::function<void(const ast::FunctionDeclaration &)> validate_const_purity;
  validate_const_purity = [&](const ast::FunctionDeclaration &function) {
    PurityState &state = purity_states[&function];
    if (state == PurityState::Complete || state == PurityState::Visiting)
      return;
    state = PurityState::Visiting;
    std::unordered_set<std::string> locals;
    for (const auto &parameter : function.parameters)
      locals.insert(parameter.name);
    std::function<void(const ast::Expression &,
                       const std::unordered_set<std::string> &)>
        check_expression;
    check_expression = [&](const ast::Expression &expression,
                           const std::unordered_set<std::string> &scope) {
      std::visit(
          [&](const auto &node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, ast::IdentifierExpression>) {
              if (scope.contains(node.name))
                return;
              std::string key = global_key(function.module_name, node.name);
              if (!globals.contains(key)) {
                if (const auto exported = public_globals.find(node.name);
                    exported != public_globals.end())
                  key = exported->second;
              }
              if (const auto global = globals.find(key);
                  global != globals.end() &&
                  global->second.declaration->declaration.is_mutable)
                throw CompileError{node.location,
                                   "const def '" + function.name +
                                       "' cannot observe mutable global '" +
                                       key + "'"};
            } else if constexpr (std::is_same_v<Node,
                                                ast::ArrayLiteralExpression>) {
              for (const auto &element : node.elements)
                check_expression(*element, scope);
            } else if constexpr (std::is_same_v<Node, ast::CallExpression>) {
              for (const auto &argument : node.arguments)
                check_expression(*argument, scope);
              if (is_constant_builtin(node.callee))
                return;
              const auto *callee = find_constant_function(node.callee);
              if (callee == nullptr)
                throw CompileError{node.location,
                                   "const def '" + function.name +
                                       "' cannot call non-constant function '" +
                                       node.callee + "'"};
              validate_const_purity(*callee);
            } else if constexpr (std::is_same_v<Node,
                                                ast::MethodCallExpression>) {
              throw CompileError{node.location,
                                 "const def '" + function.name +
                                     "' cannot perform a method/FFI/I-O call"};
            } else if constexpr (std::is_same_v<Node,
                                                ast::MemberAccessExpression>) {
              check_expression(*node.object, scope);
            } else if constexpr (std::is_same_v<Node, ast::NewExpression>) {
              for (const auto &argument : node.arguments)
                check_expression(*argument, scope);
            } else if constexpr (std::is_same_v<Node, ast::IfExpression>) {
              check_expression(*node.condition, scope);
              check_expression(*node.then_expression, scope);
              check_expression(*node.else_expression, scope);
            } else if constexpr (std::is_same_v<Node, ast::MatchExpression>) {
              check_expression(*node.scrutinee, scope);
              for (const auto &arm : node.arms) {
                auto arm_scope = scope;
                arm_scope.insert(arm.bindings.begin(), arm.bindings.end());
                if (arm.literal && !ast::is_enum_binding_pattern(arm))
                  check_expression(*arm.literal, scope);
                if (arm.guard)
                  check_expression(*arm.guard, arm_scope);
                check_expression(*arm.expression, arm_scope);
              }
            } else if constexpr (std::is_same_v<Node, ast::MoveExpression> ||
                                 std::is_same_v<Node, ast::TryExpression> ||
                                 std::is_same_v<Node, ast::UnaryExpression>) {
              check_expression(*node.operand, scope);
            } else if constexpr (std::is_same_v<Node, ast::BinaryExpression>) {
              check_expression(*node.left, scope);
              check_expression(*node.right, scope);
            }
          },
          expression.value);
    };
    std::function<void(const std::vector<ast::Statement> &,
                       std::unordered_set<std::string>)>
        check_statements;
    check_statements = [&](const std::vector<ast::Statement> &statements,
                           std::unordered_set<std::string> scope) {
      for (const ast::Statement &statement : statements) {
        if (const auto *declaration =
                std::get_if<ast::ValueDeclaration>(&statement)) {
          if (!declaration->is_constant || !declaration->initializer)
            throw CompileError{declaration->location,
                               "const def local declarations must be const"};
          check_expression(*declaration->initializer, scope);
          scope.insert(declaration->name);
        } else if (const auto *returned =
                       std::get_if<ast::ReturnStatement>(&statement)) {
          if (!returned->expression)
            throw CompileError{returned->location,
                               "const def must return a constant value"};
          check_expression(*returned->expression, scope);
        } else if (const auto *conditional =
                       std::get_if<std::shared_ptr<ast::IfStatement>>(
                           &statement)) {
          check_expression((*conditional)->condition, scope);
          check_statements((*conditional)->then_body, scope);
          check_statements((*conditional)->else_body, scope);
        } else {
          throw CompileError{function.location,
                             "const def contains an operation with runtime "
                             "effects"};
        }
      }
    };
    check_statements(function.body, locals);
    state = PurityState::Complete;
  };
  for (const ast::FunctionDeclaration &function : program.functions)
    if (function.is_constant)
      validate_const_purity(function);

  const constant::InitializationPlan initialization_plan =
      constant::plan_initialization(program);
  std::function<const constant::Value &(const std::string &)> evaluate_global;
  std::size_t constant_steps = 0;
  std::size_t constant_depth = 0;
  std::size_t constant_memory_used = 0;
  constant::EvaluationBudget evaluation_budget{
      options.constant_step_budget, options.constant_memory_budget,
      options.constant_value_size_budget};
  std::function<std::optional<constant::Value>(
      std::string_view, const std::vector<constant::Value> &, SourceLocation)>
      evaluate_constant_function;
  evaluate_constant_function =
      [&](std::string_view name, const std::vector<constant::Value> &arguments,
          SourceLocation location) -> std::optional<constant::Value> {
    const ast::FunctionDeclaration *found = find_constant_function(name);
    if (found == nullptr)
      return std::nullopt;
    const ast::FunctionDeclaration &function = *found;
    if (arguments.size() != function.parameters.size())
      throw CompileError{location, "const def '" + function.name +
                                       "' received an invalid argument count"};
    if (++constant_steps > options.constant_step_budget)
      throw CompileError{
          location, "constant evaluation step budget exceeded (" +
                        std::to_string(options.constant_step_budget) + ")"};
    if (++constant_depth > options.constant_recursion_budget)
      throw CompileError{location,
                         "constant evaluation recursion budget exceeded (" +
                             std::to_string(options.constant_recursion_budget) +
                             ")"};
    struct DepthGuard {
      std::size_t &depth;
      ~DepthGuard() { --depth; }
    } guard{constant_depth};
    std::unordered_map<std::string, constant::Value> locals;
    for (std::size_t index = 0; index < arguments.size(); ++index)
      locals.emplace(function.parameters[index].name, arguments[index]);
    const constant::Resolver local_resolver =
        [&](const std::optional<std::string> &module, std::string_view value,
            SourceLocation reference_location)
        -> std::optional<constant::Value> {
      if (!module.has_value())
        if (const auto local = locals.find(std::string{value});
            local != locals.end())
          return local->second;
      std::string key = global_key(module, value);
      if (!module.has_value() && !globals.contains(key)) {
        if (const auto exported = public_globals.find(std::string{value});
            exported != public_globals.end())
          key = exported->second;
      }
      if (!globals.contains(key))
        return std::nullopt;
      if (globals.at(key).declaration->declaration.is_mutable)
        throw CompileError{reference_location,
                           "const def cannot observe mutable global '" + key +
                               "'"};
      return evaluate_global(key);
    };
    const std::unordered_set<std::string> function_type_parameters{
        function.type_parameters.begin(), function.type_parameters.end()};
    const SemanticType return_type = resolve_type(
        function.return_type, function_type_parameters, &class_arities);
    const Type *constant_return_type = return_type.concrete;
    if (function.return_type.name == "isize")
      constant_return_type = &Type::isize_type(options.target.pointer_width);
    else if (function.return_type.name == "usize")
      constant_return_type = &Type::usize_type(options.target.pointer_width);
    return constant::evaluate_statements(
        function.body, constant_return_type, std::move(locals), local_resolver,
        constant_constructor_resolver, evaluate_constant_function,
        options.constant_step_budget, &evaluation_budget);
  };
  evaluate_global = [&](const std::string &key) -> const constant::Value & {
    const ConstantState state = constant_states[key];
    if (state == ConstantState::Visiting)
      throw CompileError{globals.at(key).declaration->declaration.location,
                         "cyclic global constant dependency involving '" + key +
                             "'"};
    if (state == ConstantState::Complete)
      return constant_values.at(key);

    constant_states[key] = ConstantState::Visiting;
    const ResolvedGlobal &resolved = globals.at(key);
    const ast::GlobalDeclaration &global = *resolved.declaration;
    const constant::Resolver resolver =
        [&](const std::optional<std::string> &qualified_module,
            std::string_view name,
            SourceLocation location) -> std::optional<constant::Value> {
      std::string dependency_key;
      if (qualified_module.has_value()) {
        dependency_key = global_key(qualified_module, name);
      } else {
        const std::string local_key = global_key(global.module_name, name);
        if (globals.contains(local_key))
          dependency_key = local_key;
        else if (const auto exported = public_globals.find(std::string{name});
                 exported != public_globals.end())
          dependency_key = exported->second;
        else
          return std::nullopt;
      }
      const auto dependency = globals.find(dependency_key);
      if (dependency == globals.end())
        return std::nullopt;
      const ast::GlobalDeclaration &target = *dependency->second.declaration;
      if (target.declaration.is_private &&
          target.module_name != global.module_name)
        throw CompileError{location, "global constant '" + dependency_key +
                                         "' is private"};
      if (target.declaration.is_internal &&
          target.module_name != global.module_name)
        throw CompileError{location, "global constant '" + dependency_key +
                                         "' is internal"};
      const std::string spelling =
          qualified_module.has_value()
              ? *qualified_module + "." + std::string{name}
              : std::string{name};
      if (!import_allows(global.module_name, target.module_name,
                         target.declaration.name, spelling))
        throw CompileError{location, "global value '" + spelling +
                                         "' is not imported in this module"};
      if (target.declaration.is_mutable)
        throw CompileError{
            location, "global constant initializer cannot depend on mutable "
                      "global '" +
                          dependency_key + "'"};
      return evaluate_global(dependency_key);
    };
    constant::Value value = constant::evaluate(
        *global.declaration.initializer, resolved.symbol.type.concrete,
        resolver, constant_constructor_resolver, evaluate_constant_function,
        &evaluation_budget);
    const std::size_t value_size = constant::canonical_serialize(value).size();
    if (value_size > options.constant_value_size_budget)
      throw CompileError{
          global.declaration.location,
          "constant value size budget exceeded (" +
              std::to_string(options.constant_value_size_budget) + " bytes)"};
    if (value_size >
        options.constant_memory_budget -
            std::min(options.constant_memory_budget, constant_memory_used))
      throw CompileError{global.declaration.location,
                         "constant evaluation memory budget exceeded (" +
                             std::to_string(options.constant_memory_budget) +
                             " bytes)"};
    constant_memory_used += value_size;
    constant_states[key] = ConstantState::Complete;
    auto [iterator, inserted] = constant_values.emplace(key, std::move(value));
    static_cast<void>(inserted);
    result.global_constant_values.insert_or_assign(key, iterator->second);
    return iterator->second;
  };
  for (const ast::GlobalDeclaration *global : initialization_plan.constants) {
    const ResolvedGlobal &resolved =
        globals.at(global_key(global->module_name, global->declaration.name));
    if (global->declaration.is_constant ||
        resolved.symbol.type.concrete != nullptr)
      static_cast<void>(evaluate_global(
          global_key(global->module_name, global->declaration.name)));
  }

  for (const ast::Program::StaticAssertion &assertion :
       program.static_assertions) {
    try {
      const constant::Value condition = constant::evaluate(
          assertion.condition, &Type::bool_type(),
          [&](const std::optional<std::string> &module, std::string_view name,
              SourceLocation location) -> std::optional<constant::Value> {
            std::string key;
            if (module.has_value()) {
              key = global_key(module, name);
            } else {
              const std::string local = global_key(assertion.module_name, name);
              if (globals.contains(local))
                key = local;
              else if (const auto exported =
                           public_globals.find(std::string{name});
                       exported != public_globals.end())
                key = exported->second;
              else
                return std::nullopt;
            }
            const auto found = globals.find(key);
            if (found == globals.end() ||
                !found->second.declaration->declaration.is_constant)
              return std::nullopt;
            const ast::GlobalDeclaration &target = *found->second.declaration;
            if (target.declaration.is_private &&
                target.module_name != assertion.module_name)
              throw CompileError{location,
                                 "global constant '" + key + "' is private"};
            if (target.declaration.is_internal &&
                target.module_name != assertion.module_name)
              throw CompileError{location,
                                 "global constant '" + key + "' is internal"};
            const std::string spelling = module.has_value()
                                             ? *module + "." + std::string{name}
                                             : std::string{name};
            if (!import_allows(assertion.module_name, target.module_name,
                               target.declaration.name, spelling))
              throw CompileError{location,
                                 "global value '" + spelling +
                                     "' is not imported in this module"};
            return evaluate_global(key);
          },
          constant_constructor_resolver, evaluate_constant_function,
          &evaluation_budget);
      if (!std::get<bool>(condition.data))
        throw CompileError{Diagnostic{DiagnosticSeverity::Error,
                                      DiagnosticCode::Unclassified,
                                      "static assertion failed" +
                                          (assertion.message.has_value()
                                               ? ": " + *assertion.message
                                               : std::string{}),
                                      assertion.location,
                                      {},
                                      {},
                                      {},
                                      assertion.source_path}};
    } catch (const CompileError &error) {
      if (std::string_view{error.what()}.find("static assertion failed") !=
          std::string_view::npos)
        throw;
      throw CompileError{Diagnostic{
          DiagnosticSeverity::Error,
          DiagnosticCode::Unclassified,
          "static assertion condition is not a constant expression: " +
              std::string{error.what()},
          assertion.location,
          {},
          {},
          {},
          assertion.source_path}};
    }
  }

  struct TraitInstance {
    const ast::TraitDeclaration *declaration;
    std::vector<SemanticType> type_arguments;
  };
  const auto resolve_trait =
      [&](const ast::TypeReference &reference,
          const std::unordered_set<std::string> &type_parameters) {
        const auto iterator = traits.find(reference.name);
        if (iterator == traits.end())
          throw CompileError{reference.location,
                             "unknown trait '" + reference.name + "'"};
        const ast::TraitDeclaration &declaration = *iterator->second;
        if (reference.type_arguments.size() !=
            declaration.type_parameters.size())
          throw CompileError{
              reference.location,
              "trait '" + declaration.name + "' expects " +
                  std::to_string(declaration.type_parameters.size()) +
                  " type argument(s), got " +
                  std::to_string(reference.type_arguments.size())};
        std::vector<SemanticType> arguments;
        arguments.reserve(reference.type_arguments.size());
        for (const ast::TypeReference &argument : reference.type_arguments)
          arguments.push_back(
              resolve_type(argument, type_parameters, &class_arities));
        return TraitInstance{&declaration, std::move(arguments)};
      };
  const auto satisfies_trait = [&](const SemanticType &candidate,
                                   const TraitInstance &requirement) {
    if (!candidate.is_class())
      return false;
    const ast::ClassDeclaration &class_declaration =
        *classes.at(candidate.parameter);
    std::unordered_map<std::string, SemanticType> class_substitutions;
    for (std::size_t index = 0;
         index < class_declaration.type_parameters.size(); ++index)
      class_substitutions.emplace(class_declaration.type_parameters[index],
                                  candidate.type_arguments[index]);
    const std::unordered_set<std::string> class_parameters{
        class_declaration.type_parameters.begin(),
        class_declaration.type_parameters.end()};
    for (const ast::TypeReference &implemented :
         class_declaration.implemented_traits) {
      if (implemented.name != requirement.declaration->name)
        continue;
      const TraitInstance instance =
          resolve_trait(implemented, class_parameters);
      if (instance.type_arguments.size() != requirement.type_arguments.size())
        continue;
      bool matches = true;
      for (std::size_t index = 0; index < instance.type_arguments.size();
           ++index)
        matches =
            matches && same_type(substitute(instance.type_arguments[index],
                                            class_substitutions),
                                 requirement.type_arguments[index]);
      if (matches)
        return true;
    }
    return false;
  };
  const auto validate_constraints =
      [&](const std::vector<ast::TypeConstraint> &constraints,
          const std::unordered_set<std::string> &type_parameters) {
        std::unordered_set<std::string> declared_constraints;
        for (const ast::TypeConstraint &constraint : constraints) {
          if (!type_parameters.contains(constraint.parameter))
            throw CompileError{constraint.location,
                               "constraint targets unknown type parameter '" +
                                   constraint.parameter + "'"};
          const std::string key =
              constraint.parameter + ":" + constraint.trait.name;
          if (!declared_constraints.insert(key).second)
            throw CompileError{
                constraint.location,
                "trait constraint '" + constraint.trait.name +
                    "' is already declared for type parameter '" +
                    constraint.parameter + "'"};
          if (derivation_constraint(constraint.trait.name).has_value() &&
              constraint.trait.type_arguments.empty()) {
          } else {
            static_cast<void>(resolve_trait(constraint.trait, type_parameters));
          }
        }
      };

  for (const ast::TraitDeclaration &trait_declaration : program.traits) {
    std::unordered_set<std::string> trait_parameters;
    for (const std::string &parameter : trait_declaration.type_parameters) {
      if (!trait_parameters.insert(parameter).second)
        throw CompileError{trait_declaration.location,
                           "type parameter '" + parameter +
                               "' is already declared"};
      if (builtin_type(parameter) != nullptr || parameter == "Function")
        throw CompileError{trait_declaration.location,
                           "type parameter '" + parameter +
                               "' conflicts with a built-in type"};
    }
    validate_constraints(trait_declaration.type_constraints, trait_parameters);
    std::unordered_set<std::string> method_names;
    for (const ast::FunctionDeclaration &method : trait_declaration.methods) {
      if (!method_names.insert(method.name).second)
        throw CompileError{method.location,
                           "trait method '" + method.name +
                               "' is already declared in trait '" +
                               trait_declaration.name + "'"};
      std::unordered_set<std::string> method_parameters = trait_parameters;
      for (const std::string &parameter : method.type_parameters) {
        if (!method_parameters.insert(parameter).second)
          throw CompileError{method.location, "type parameter '" + parameter +
                                                  "' is already declared"};
        if (builtin_type(parameter) != nullptr || parameter == "Function")
          throw CompileError{method.location,
                             "type parameter '" + parameter +
                                 "' conflicts with a built-in type"};
      }
      validate_constraints(method.type_constraints, method_parameters);
      std::unordered_set<std::string> value_parameters;
      for (const ast::FunctionDeclaration::Parameter &parameter :
           method.parameters) {
        if (!value_parameters.insert(parameter.name).second)
          throw CompileError{parameter.location, "parameter '" +
                                                     parameter.name +
                                                     "' is already declared"};
        const SemanticType type =
            resolve_type(parameter.type, method_parameters, &class_arities);
        if (type.is_concrete() && type.concrete->kind() == TypeKind::Unit)
          throw CompileError{parameter.location,
                             "Unit cannot be used as a parameter type"};
      }
      static_cast<void>(
          resolve_type(method.return_type, method_parameters, &class_arities));
    }
  }
  for (const ast::EnumDeclaration &enum_declaration : program.enums) {
    const std::unordered_set<std::string> parameters{
        enum_declaration.type_parameters.begin(),
        enum_declaration.type_parameters.end()};
    for (const ast::EnumDeclaration::Case &enum_case : enum_declaration.cases) {
      for (const ast::TypeReference &payload_type : enum_case.payload_types) {
        const SemanticType resolved =
            resolve_type(payload_type, parameters, &class_arities);
        if (resolved.is_concrete() &&
            resolved.concrete->kind() == TypeKind::Unit)
          throw CompileError{payload_type.location,
                             "Unit cannot be stored in an enum variant"};
      }
    }
  }
  for (const ast::ClassDeclaration &class_declaration : program.classes) {
    std::unordered_set<std::string> parameters;
    for (const std::string &parameter : class_declaration.type_parameters) {
      if (!parameters.insert(parameter).second)
        throw CompileError{class_declaration.location,
                           "type parameter '" + parameter +
                               "' is already declared"};
      if (builtin_type(parameter) != nullptr || parameter == "Function")
        throw CompileError{class_declaration.location,
                           "type parameter '" + parameter +
                               "' conflicts with a built-in type"};
    }
    validate_constraints(class_declaration.type_constraints, parameters);
    if (class_declaration.is_value_type) {
      if (!class_declaration.constructor_parameters.empty())
        throw CompileError{class_declaration.location,
                           "struct constructors only support val/var fields"};
      if (!class_declaration.fields.empty())
        throw CompileError{class_declaration.location,
                           "struct fields must be declared in the constructor"};
      if (class_declaration.destructor.has_value())
        throw CompileError{class_declaration.location,
                           "struct values cannot declare a destructor"};
      if (!class_declaration.implemented_traits.empty())
        throw CompileError{
            class_declaration.location,
            "struct trait implementations are not supported yet"};
    }
    std::unordered_set<std::string> constructor_names;
    for (const ast::FunctionDeclaration::Parameter &parameter :
         class_declaration.constructor_parameters) {
      if (!constructor_names.insert(parameter.name).second)
        throw CompileError{parameter.location, "constructor parameter '" +
                                                   parameter.name +
                                                   "' is already declared"};
      const SemanticType parameter_type =
          resolve_type(parameter.type, parameters, &class_arities);
      if (parameter_type.is_concrete() &&
          parameter_type.concrete->kind() == TypeKind::Unit)
        throw CompileError{parameter.location,
                           "Unit cannot be used as a parameter type"};
    }
    for (const ast::ValueDeclaration &field :
         class_declaration.constructor_fields) {
      if (!constructor_names.insert(field.name).second)
        throw CompileError{field.location, "constructor parameter '" +
                                               field.name +
                                               "' is already declared"};
      const SemanticType field_type =
          resolve_type(*field.declared_type, parameters, &class_arities);
      if (field_type.is_concrete() &&
          field_type.concrete->kind() == TypeKind::Unit)
        throw CompileError{field.location,
                           "Unit cannot be used as a field type"};
    }
    for (const ast::ValueDeclaration &field : class_declaration.fields) {
      const SemanticType field_type =
          resolve_type(*field.declared_type, parameters, &class_arities);
      if (field_type.is_concrete() &&
          field_type.concrete->kind() == TypeKind::Unit)
        throw CompileError{field.location,
                           "Unit cannot be used as a field type"};
    }

    std::unordered_set<std::string> implemented_trait_names;
    for (const ast::TypeReference &trait_reference :
         class_declaration.implemented_traits) {
      const auto trait_iterator = traits.find(trait_reference.name);
      if (trait_iterator == traits.end())
        throw CompileError{trait_reference.location,
                           "unknown trait '" + trait_reference.name + "'"};
      const ast::TraitDeclaration &trait_declaration = *trait_iterator->second;
      if (!implemented_trait_names.insert(trait_declaration.name).second)
        throw CompileError{trait_reference.location,
                           "trait '" + trait_declaration.name +
                               "' is already implemented by class '" +
                               class_declaration.name + "'"};
      if (trait_reference.type_arguments.size() !=
          trait_declaration.type_parameters.size())
        throw CompileError{
            trait_reference.location,
            "trait '" + trait_declaration.name + "' expects " +
                std::to_string(trait_declaration.type_parameters.size()) +
                " type argument(s), got " +
                std::to_string(trait_reference.type_arguments.size())};

      std::unordered_map<std::string, SemanticType> trait_substitutions;
      for (std::size_t index = 0;
           index < trait_declaration.type_parameters.size(); ++index) {
        trait_substitutions.emplace(
            trait_declaration.type_parameters[index],
            resolve_type(trait_reference.type_arguments[index], parameters,
                         &class_arities));
      }

      for (const ast::FunctionDeclaration &required :
           trait_declaration.methods) {
        const auto implementation = std::find_if(
            class_declaration.methods.begin(), class_declaration.methods.end(),
            [&](const ast::FunctionDeclaration &candidate) {
              return candidate.name == required.name;
            });
        if (implementation == class_declaration.methods.end())
          throw CompileError{class_declaration.location,
                             "class '" + class_declaration.name +
                                 "' does not implement trait method '" +
                                 trait_declaration.name + "." + required.name +
                                 "'"};
        if (implementation->is_private)
          throw CompileError{implementation->location,
                             "private method '" + implementation->name +
                                 "' cannot implement public trait method '" +
                                 trait_declaration.name + "." + required.name +
                                 "'"};
        if (implementation->type_parameters.size() !=
                required.type_parameters.size() ||
            implementation->parameters.size() != required.parameters.size())
          throw CompileError{implementation->location,
                             "method '" + class_declaration.name + "." +
                                 implementation->name +
                                 "' has a signature incompatible with trait '" +
                                 trait_declaration.name + "'"};
        if (implementation->is_consuming != required.is_consuming ||
            implementation->is_borrowing != required.is_borrowing)
          throw CompileError{
              implementation->location,
              "method '" + class_declaration.name + "." + implementation->name +
                  "' has an ownership contract incompatible with trait '" +
                  trait_declaration.name + "'"};

        std::unordered_set<std::string> trait_method_parameters{
            trait_declaration.type_parameters.begin(),
            trait_declaration.type_parameters.end()};
        std::unordered_set<std::string> class_method_parameters = parameters;
        std::unordered_map<std::string, SemanticType> trait_method_canonical;
        std::unordered_map<std::string, SemanticType> class_method_canonical;
        for (std::size_t index = 0; index < required.type_parameters.size();
             ++index) {
          trait_method_parameters.insert(required.type_parameters[index]);
          class_method_parameters.insert(
              implementation->type_parameters[index]);
          const SemanticType canonical{nullptr,
                                       "$method" + std::to_string(index)};
          trait_method_canonical.emplace(required.type_parameters[index],
                                         canonical);
          class_method_canonical.emplace(implementation->type_parameters[index],
                                         canonical);
        }
        const auto required_type = [&](const ast::TypeReference &reference) {
          return substitute(
              substitute(resolve_type(reference, trait_method_parameters,
                                      &class_arities),
                         trait_substitutions),
              trait_method_canonical);
        };
        const auto implemented_type = [&](const ast::TypeReference &reference) {
          return substitute(
              resolve_type(reference, class_method_parameters, &class_arities),
              class_method_canonical);
        };
        bool compatible =
            same_type(required_type(required.return_type),
                      implemented_type(implementation->return_type));
        for (std::size_t index = 0;
             compatible && index < required.parameters.size(); ++index)
          compatible = required.parameters[index].ownership ==
                           implementation->parameters[index].ownership &&
                       same_type(required_type(required.parameters[index].type),
                                 implemented_type(
                                     implementation->parameters[index].type));
        if (!compatible)
          throw CompileError{implementation->location,
                             "method '" + class_declaration.name + "." +
                                 implementation->name +
                                 "' has a signature incompatible with trait '" +
                                 trait_declaration.name + "'"};
      }
    }
  }

  struct FunctionContext {
    const ast::FunctionDeclaration *function;
    const ast::ClassDeclaration *owner;
    const ast::DestructorDeclaration *destructor;
    const ast::GlobalDeclaration *global;
  };
  std::vector<FunctionContext> contexts;
  std::unordered_map<std::string, std::size_t> function_name_counts;
  for (const ast::FunctionDeclaration &function : program.functions) {
    contexts.push_back(FunctionContext{&function, nullptr, nullptr, nullptr});
    const std::string identity =
        global_key(function.module_name, function.name);
    if (!functions.emplace(identity, &function).second)
      throw CompileError{function.location,
                         "function '" + identity + "' is already declared"};
    ++function_name_counts[function.name];
    for (const std::string &alias :
         imported_names(function.module_name, function.name))
      functions.emplace(alias, &function);
  }
  std::unordered_set<std::string> ambiguous_functions;
  for (const ast::FunctionDeclaration &function : program.functions)
    if (function_name_counts.at(function.name) == 1)
      functions.emplace(function.name, &function);
    else
      ambiguous_functions.insert(function.name);
  std::vector<ast::FunctionDeclaration> global_initializer_functions;
  global_initializer_functions.reserve(initialization_plan.dynamic.size());
  for (const ast::GlobalDeclaration *global_pointer :
       initialization_plan.dynamic) {
    const ast::GlobalDeclaration &global = *global_pointer;
    global_initializer_functions.push_back(ast::FunctionDeclaration{
        "__global_init_" + global.declaration.name,
        {},
        {},
        ast::TypeReference{"Unit", global.declaration.location, {}},
        {},
        global.declaration.location,
        false,
        false,
        {},
        false,
        std::nullopt,
        false,
        global.module_name,
        false,
        {}});
    contexts.push_back(FunctionContext{&global_initializer_functions.back(),
                                       nullptr, nullptr, &global});
  }
  std::unordered_map<std::string, const ast::FunctionDeclaration *>
      external_symbols;
  for (const ast::FunctionDeclaration &function : program.functions) {
    if (!function.is_external)
      continue;
    const std::string &symbol = function.external_symbol.has_value()
                                    ? *function.external_symbol
                                    : function.name;
    if (symbol.empty())
      throw CompileError{function.location,
                         "external symbol name cannot be empty"};
    if (symbol.find('\0') != std::string::npos)
      throw CompileError{function.location,
                         "external symbol name cannot contain a null byte"};
    if (const auto existing = external_symbols.find(symbol);
        existing != external_symbols.end() &&
        existing->second->name != function.name)
      throw CompileError{function.location,
                         "external symbol '" + symbol +
                             "' is already bound to function '" +
                             existing->second->name + "'"};
    external_symbols.emplace(symbol, &function);
    if (symbol != function.name) {
      const auto collision = functions.find(symbol);
      if (collision != functions.end() && !collision->second->is_external)
        throw CompileError{function.location,
                           "external symbol '" + symbol +
                               "' conflicts with Janus function '" + symbol +
                               "'"};
    }
  }
  for (const ast::ClassDeclaration &class_declaration : program.classes) {
    std::unordered_set<std::string> method_names;
    for (const ast::FunctionDeclaration &method : class_declaration.methods) {
      if (!method_names.insert(method.name).second) {
        throw CompileError{method.location,
                           "method '" + method.name +
                               "' is already declared in class '" +
                               class_declaration.name + "'"};
      }
      contexts.push_back(
          FunctionContext{&method, &class_declaration, nullptr, nullptr});
    }
    if (class_declaration.destructor.has_value())
      contexts.push_back(FunctionContext{nullptr, &class_declaration,
                                         &*class_declaration.destructor,
                                         nullptr});
  }

  const auto main_iterator = functions.find("main");
  if (options.require_entry_point && main_iterator == functions.end()) {
    throw CompileError{SourceLocation{},
                       "program must declare an entry point 'main'"};
  }

  for (const FunctionContext &context : contexts) {
    const bool is_destructor = context.destructor != nullptr;
    const bool is_global_initializer = context.global != nullptr;
    const ast::ClassDeclaration *owner = context.owner;
    const std::vector<std::string> empty_type_parameters;
    const std::vector<ast::FunctionDeclaration::Parameter> empty_parameters;
    const std::vector<std::string> &function_type_parameters =
        is_destructor ? empty_type_parameters
                      : context.function->type_parameters;
    const std::vector<ast::FunctionDeclaration::Parameter> &parameters =
        is_destructor ? empty_parameters : context.function->parameters;
    const std::vector<ast::Statement> &body =
        is_destructor ? context.destructor->body : context.function->body;
    const SourceLocation function_location = is_destructor
                                                 ? context.destructor->location
                                                 : context.function->location;
    const std::string function_name =
        is_destructor ? "destructor" : context.function->name;
    std::optional<std::string> context_module =
        owner != nullptr ? owner->module_name : context.function->module_name;
    std::unordered_set<std::string> type_parameters;
    if (owner != nullptr) {
      type_parameters.insert(owner->type_parameters.begin(),
                             owner->type_parameters.end());
    }
    for (const std::string &parameter : function_type_parameters) {
      if (!type_parameters.insert(parameter).second) {
        throw CompileError{function_location, "type parameter '" + parameter +
                                                  "' is already declared"};
      }
      if (builtin_type(parameter) != nullptr || parameter == "Function") {
        throw CompileError{function_location,
                           "type parameter '" + parameter +
                               "' conflicts with a built-in type"};
      }
    }
    if (!is_destructor && context.function->is_external &&
        (!function_type_parameters.empty() ||
         !context.function->type_constraints.empty()))
      throw CompileError{function_location, "external function '" +
                                                function_name +
                                                "' cannot be generic"};
    if (!is_destructor && context.function->is_variadic) {
      if (!context.function->is_external)
        throw CompileError{function_location,
                           "only external functions can be variadic"};
      if (parameters.empty())
        throw CompileError{
            function_location,
            "variadic external function requires a fixed parameter"};
    }
    if (!is_destructor)
      validate_constraints(context.function->type_constraints, type_parameters);

    const SemanticType return_type =
        is_destructor ? SemanticType{&Type::unit_type()}
                      : resolve_type(context.function->return_type,
                                     type_parameters, &class_arities,
                                     context_module, &scoped_type_aliases);
    if (!is_destructor && owner == nullptr && function_name == "main") {
      if (context.function->is_external)
        throw CompileError{function_location,
                           "entry point 'main' cannot be external"};
      if (!function_type_parameters.empty() || !parameters.empty() ||
          !return_type.is_concrete() ||
          return_type.concrete->kind() != TypeKind::Int) {
        throw CompileError{
            function_location,
            "entry point must have signature 'def main() : int'"};
      }
    }

    SymbolTable symbols;
    std::unordered_set<std::string> owner_field_names;
    if (owner != nullptr) {
      std::vector<SemanticType> owner_arguments;
      for (const std::string &parameter : owner->type_parameters)
        owner_arguments.push_back(SemanticType{nullptr, parameter});
      symbols.emplace("this", Symbol{SemanticType{nullptr, owner->name, true,
                                                  std::move(owner_arguments)},
                                     false, true});
      for (const ast::ValueDeclaration &field : owner->constructor_fields) {
        owner_field_names.insert(field.name);
        symbols.emplace(
            field.name,
            Symbol{resolve_type(*field.declared_type, type_parameters,
                                &class_arities, context_module,
                                &scoped_type_aliases),
                   field.is_mutable &&
                       (is_destructor || !context.function->is_borrowing),
                   true});
      }
      for (const ast::ValueDeclaration &field : owner->fields) {
        owner_field_names.insert(field.name);
        symbols.emplace(
            field.name,
            Symbol{resolve_type(*field.declared_type, type_parameters,
                                &class_arities, context_module,
                                &scoped_type_aliases),
                   field.is_mutable &&
                       (is_destructor || !context.function->is_borrowing),
                   field.initializer.has_value()});
      }
    }
    for (const ast::FunctionDeclaration::Parameter &parameter : parameters) {
      const SemanticType parameter_type =
          resolve_type(parameter.type, type_parameters, &class_arities,
                       context_module, &scoped_type_aliases);
      if (parameter.ownership == ast::ParameterOwnership::Consume &&
          (is_destructor || !context.function->is_external))
        throw CompileError{
            parameter.location,
            "consume parameter qualifiers are only supported on external "
            "functions"};
      if (parameter.ownership != ast::ParameterOwnership::Unspecified &&
          context.function->is_external && !parameter_type.is_pointer())
        throw CompileError{
            parameter.location,
            "borrow and consume external parameters require a Ptr[T] type"};
      if (parameter_type.is_concrete() &&
          parameter_type.concrete->kind() == TypeKind::Unit)
        throw CompileError{parameter.location,
                           "Unit cannot be used as a parameter type"};
      if (!symbols.emplace(parameter.name, Symbol{parameter_type, false, true})
               .second) {
        throw CompileError{parameter.location, "value '" + parameter.name +
                                                   "' is already declared"};
      }
    }
    if (!is_destructor &&
        context.function->return_ownership !=
            ast::ReturnOwnership::Unspecified &&
        !context.function->is_external)
      throw CompileError{
          context.function->return_type.location,
          "borrow and owned return qualifiers are only supported on external "
          "functions"};
    if (!is_destructor &&
        context.function->return_ownership !=
            ast::ReturnOwnership::Unspecified &&
        !return_type.is_pointer())
      throw CompileError{
          context.function->return_type.location,
          "borrow and owned external returns require a Ptr[T] type"};
    if (!is_destructor && context.function->is_external) {
      if (!is_c_abi_type(return_type, true))
        throw CompileError{context.function->return_type.location,
                           "external function return type '" +
                               return_type.name() +
                               "' is not compatible with the C ABI"};
      for (const ast::FunctionDeclaration::Parameter &parameter : parameters) {
        const SemanticType parameter_type =
            resolve_type(parameter.type, type_parameters, &class_arities,
                         context_module, &scoped_type_aliases);
        if (!is_c_abi_type(parameter_type, false))
          throw CompileError{parameter.location,
                             "external parameter '" + parameter.name +
                                 "' has type '" + parameter_type.name() +
                                 "', which is not compatible with the C ABI"};
      }
      result.functions.emplace(function_name, std::move(symbols));
      continue;
    }
    std::unordered_map<std::string, std::vector<TraitInstance>>
        active_trait_constraints;
    std::unordered_set<std::string> active_copy_constraints;
    const auto add_active_constraints =
        [&](const std::vector<ast::TypeConstraint> &constraints) {
          for (const ast::TypeConstraint &constraint : constraints) {
            if (const auto kind = derivation_constraint(constraint.trait.name);
                kind.has_value() && constraint.trait.type_arguments.empty()) {
              if (*kind == ast::DerivationKind::Copy)
                active_copy_constraints.insert(constraint.parameter);
              continue;
            }
            active_trait_constraints[constraint.parameter].push_back(
                resolve_trait(constraint.trait, type_parameters));
          }
        };
    if (owner != nullptr)
      add_active_constraints(owner->type_constraints);
    if (!is_destructor)
      add_active_constraints(context.function->type_constraints);
    const auto is_copy_only_array_method = [](std::string_view name) {
      return name == "iterator" || name == "get" || name == "getOption" ||
             name == "map" || name == "filter" || name == "find" ||
             name == "fold" || name == "any" || name == "all" ||
             name == "count" || name == "sortWith";
    };
    if (!is_destructor && owner != nullptr && owner->name == "Array" &&
        owner->module_name == std::optional<std::string>{"std.array"} &&
        is_copy_only_array_method(context.function->name))
      active_copy_constraints.insert("T");
    if (!is_destructor && owner != nullptr &&
        owner->module_name == std::optional<std::string>{"std.hashset"} &&
        ((owner->name == "HashSet" && (context.function->name == "iterator" ||
                                       context.function->name == "valueAt")) ||
         (owner->name == "SetBuilder" && context.function->name == "addAll")))
      active_copy_constraints.insert("T");
    if (!is_destructor && owner != nullptr && owner->name == "HashMap" &&
        owner->module_name == std::optional<std::string>{"std.hashmap"}) {
      if (context.function->name == "entryAt" ||
          context.function->name == "entries") {
        active_copy_constraints.insert("K");
        active_copy_constraints.insert("V");
      }
      if (context.function->name == "keyAt" || context.function->name == "keys")
        active_copy_constraints.insert("K");
      if (context.function->name == "valueAt" ||
          context.function->name == "getOption" ||
          context.function->name == "values")
        active_copy_constraints.insert("V");
    }
    if (!is_destructor && owner != nullptr && owner->name == "ArrayBuilder" &&
        owner->module_name == std::optional<std::string>{"std.array_builder"} &&
        context.function->name == "addAll")
      active_copy_constraints.insert("T");
    const auto satisfies_active_trait = [&](const SemanticType &candidate,
                                            const TraitInstance &requirement) {
      if (satisfies_trait(candidate, requirement))
        return true;
      const auto iterator = active_trait_constraints.find(candidate.parameter);
      if (candidate.is_concrete() || candidate.is_class() ||
          iterator == active_trait_constraints.end())
        return false;
      return std::any_of(
          iterator->second.begin(), iterator->second.end(),
          [&](const TraitInstance &active) {
            return active.declaration == requirement.declaration &&
                   active.type_arguments.size() ==
                       requirement.type_arguments.size() &&
                   std::equal(active.type_arguments.begin(),
                              active.type_arguments.end(),
                              requirement.type_arguments.begin(), same_type);
          });
    };
    const auto potentially_owns_value = [&](const SemanticType &candidate) {
      return janus::ownership::recursively_owns_value(
          candidate,
          [&](const SemanticType &type) {
            if (type.is_function() || type.is_pointer())
              return true;
            if (type.is_class())
              return !classes.at(type.parameter)->is_value_type;
            if (!type.is_concrete() && !type.is_enum())
              return !active_copy_constraints.contains(type.parameter);
            return false;
          },
          [&](const SemanticType &type, const auto &visit) {
            if (type.is_class()) {
              const ast::ClassDeclaration &declaration =
                  *classes.at(type.parameter);
              if (!declaration.is_value_type)
                return;
              const std::unordered_set<std::string> parameters{
                  declaration.type_parameters.begin(),
                  declaration.type_parameters.end()};
              std::unordered_map<std::string, SemanticType> substitutions;
              for (std::size_t index = 0;
                   index < declaration.type_parameters.size(); ++index)
                substitutions.emplace(declaration.type_parameters[index],
                                      type.type_arguments[index]);
              for (const ast::ValueDeclaration &field :
                   declaration.constructor_fields) {
                SemanticType field_type = resolve_type(
                    *field.declared_type, parameters, &class_arities);
                visit(substitute(std::move(field_type), substitutions));
              }
              return;
            }
            if (!type.is_enum())
              return;
            const ast::EnumDeclaration &declaration = *enums.at(type.parameter);
            const std::unordered_set<std::string> parameters{
                declaration.type_parameters.begin(),
                declaration.type_parameters.end()};
            std::unordered_map<std::string, SemanticType> substitutions;
            for (std::size_t index = 0;
                 index < declaration.type_parameters.size(); ++index)
              substitutions.emplace(declaration.type_parameters[index],
                                    type.type_arguments[index]);
            for (const ast::EnumDeclaration::Case &enum_case :
                 declaration.cases)
              for (const ast::TypeReference &payload :
                   enum_case.payload_types) {
                SemanticType payload_type =
                    resolve_type(payload, parameters, &class_arities);
                visit(substitute(std::move(payload_type), substitutions));
              }
          });
    };
    const auto satisfies_copy = [&](const SemanticType &candidate) {
      return !potentially_owns_value(candidate);
    };
    const auto supports_derivation = [&](const SemanticType &candidate,
                                         ast::DerivationKind kind) -> bool {
      std::unordered_set<std::string> visiting;
      const auto visit = [&](const auto &self,
                             const SemanticType &type) -> bool {
        if (type.is_concrete())
          return type.concrete->kind() != TypeKind::Unit;
        if (type.is_pointer() || type.is_function())
          return false;
        if (!type.is_class() && !type.is_enum())
          return true;
        if (!visiting.insert(type.name()).second)
          return false;
        bool supported = true;
        if (type.is_class()) {
          const ast::ClassDeclaration &declaration =
              *classes.at(type.parameter);
          supported = has_derivation(declaration.derivations, kind);
          std::unordered_map<std::string, SemanticType> substitutions;
          for (std::size_t index = 0;
               index < declaration.type_parameters.size(); ++index)
            substitutions.emplace(declaration.type_parameters[index],
                                  type.type_arguments[index]);
          const std::unordered_set<std::string> parameters{
              declaration.type_parameters.begin(),
              declaration.type_parameters.end()};
          const auto check_field = [&](const ast::ValueDeclaration &field) {
            SemanticType field_type =
                resolve_type(*field.declared_type, parameters, &class_arities);
            return self(self, substitute(std::move(field_type), substitutions));
          };
          for (const ast::ValueDeclaration &field :
               declaration.constructor_fields)
            supported = supported && check_field(field);
          for (const ast::ValueDeclaration &field : declaration.fields)
            supported = supported && check_field(field);
        } else {
          const ast::EnumDeclaration &declaration = *enums.at(type.parameter);
          supported = has_derivation(declaration.derivations, kind);
          std::unordered_map<std::string, SemanticType> substitutions;
          const std::unordered_set<std::string> parameters{
              declaration.type_parameters.begin(),
              declaration.type_parameters.end()};
          for (std::size_t index = 0;
               index < declaration.type_parameters.size(); ++index)
            substitutions.emplace(declaration.type_parameters[index],
                                  type.type_arguments[index]);
          for (const ast::EnumDeclaration::Case &enum_case : declaration.cases)
            for (const ast::TypeReference &payload : enum_case.payload_types) {
              SemanticType payload_type =
                  resolve_type(payload, parameters, &class_arities);
              supported =
                  supported && self(self, substitute(std::move(payload_type),
                                                     substitutions));
            }
        }
        visiting.erase(type.name());
        return supported;
      };
      return visit(visit, candidate);
    };
    SymbolTable *active_symbols = &symbols;
    const auto find_global =
        [&](const std::optional<std::string> &module,
            std::string_view name) -> const ResolvedGlobal * {
      const auto iterator = globals.find(global_key(module, name));
      return iterator == globals.end() ? nullptr : &iterator->second;
    };
    const auto visible_global = [&](std::string_view name) -> const Symbol * {
      if (const ResolvedGlobal *local = find_global(context_module, name))
        return &local->symbol;
      const auto exported = public_globals.find(std::string{name});
      if (exported == public_globals.end())
        return nullptr;
      const ResolvedGlobal &resolved = globals.at(exported->second);
      if (!import_allows(context_module, resolved.declaration->module_name,
                         resolved.declaration->declaration.name, name))
        return nullptr;
      return &resolved.symbol;
    };
    const std::unordered_set<std::string> *active_type_parameters =
        &type_parameters;
    const std::unordered_map<std::string, SemanticType>
        *active_type_substitutions = nullptr;
    bool inside_lambda = false;
    bool inside_defer = false;
    bool contextual_borrow_lambda_parameters = false;
    bool contextual_borrow_expression = false;
    bool contextual_borrow_pointer_expression = false;
    std::string_view contextual_borrow_enum_name;
    std::size_t loop_depth = 0;
    std::unordered_set<std::string> transfer_protected_values;
    std::unordered_set<std::string> match_guard_protected_values;
    std::unordered_set<std::string> deferred_values;
    std::unordered_set<std::string> borrowed_values;
    std::unordered_set<std::string> shared_borrow_values;
    std::unordered_map<std::string, std::unordered_set<std::string>>
        borrow_sources;
    const auto require_no_live_borrow = [&](std::string_view owner_name,
                                            SourceLocation location,
                                            std::string_view action =
                                                "released") {
      const auto depends_on =
          [&](const auto &self, std::string_view candidate,
              std::string_view owner,
              std::unordered_set<std::string> &visited) -> bool {
        if (candidate == owner)
          return true;
        if (!visited.insert(std::string{candidate}).second)
          return false;
        const auto sources = borrow_sources.find(std::string{candidate});
        if (sources == borrow_sources.end())
          return false;
        return std::any_of(sources->second.begin(), sources->second.end(),
                           [&](const std::string &source) {
                             return self(self, source, owner, visited);
                           });
      };
      for (const auto &[borrower, sources] : borrow_sources) {
        if (action != "released" && !shared_borrow_values.contains(borrower))
          continue;
        const auto symbol = active_symbols->find(borrower);
        if (symbol == active_symbols->end() ||
            !symbol->second.may_be_initialized)
          continue;
        std::unordered_set<std::string> visited;
        const bool borrows_owner = std::any_of(
            sources.begin(), sources.end(), [&](const std::string &source) {
              return depends_on(depends_on, source, owner_name, visited);
            });
        if (!borrows_owner)
          continue;
        throw CompileError{location,
                           "owning value '" + std::string{owner_name} +
                               "' cannot be " + std::string{action} +
                               " while borrowed by '" + borrower + "'"};
      }
    };
    const auto require_guard_transfer_allowed =
        [&](const ast::Expression &expression, SourceLocation location) {
          const auto *identifier =
              std::get_if<ast::IdentifierExpression>(&expression.value);
          if (identifier != nullptr &&
              match_guard_protected_values.contains(identifier->name))
            throw CompileError{
                location,
                "pattern binding '" + identifier->name +
                    "' cannot be transferred or destroyed in a match guard"};
        };
    std::unordered_map<std::string, SourceLocation> local_declarations;
    std::unordered_set<std::size_t> warned_leak_locations;
    std::unordered_set<std::size_t> warned_unannotated_return_locations;
    std::unordered_set<std::size_t> used_local_declarations;
    std::unordered_map<std::size_t, std::vector<std::string>> lambda_captures;
    std::unordered_map<std::string, std::size_t> local_lambda_locations;
    std::unordered_set<std::string> *active_lambda_captures = nullptr;
    if (owner != nullptr)
      for (const ast::ValueDeclaration &field : owner->constructor_fields)
        if (field.is_borrowed) {
          borrowed_values.insert(field.name);
          transfer_protected_values.insert(field.name);
        }
    if (!is_destructor) {
      for (const ast::FunctionDeclaration::Parameter &parameter : parameters)
        if (parameter.ownership == ast::ParameterOwnership::Borrow) {
          borrowed_values.insert(parameter.name);
          shared_borrow_values.insert(parameter.name);
          transfer_protected_values.insert(parameter.name);
        }
      if (owner != nullptr && context.function->is_borrowing) {
        borrowed_values.insert("this");
        shared_borrow_values.insert("this");
        transfer_protected_values.insert("this");
        for (const std::string &field : owner_field_names) {
          borrowed_values.insert(field);
          shared_borrow_values.insert(field);
          transfer_protected_values.insert(field);
        }
      }
    }
    const bool report_local_warnings = context_module == program.module_name;
    const auto emit_warning =
        [&](DiagnosticCode code, SourceLocation location, std::string message,
            std::vector<std::string> notes = {},
            std::vector<DiagnosticLocation> secondary_locations = {}) {
          if (!report_local_warnings)
            return;
          result.diagnostics.push_back(Diagnostic{
              DiagnosticSeverity::Warning,
              code,
              std::move(message),
              location,
              std::move(notes),
              std::move(secondary_locations),
              {},
          });
        };
    const auto warn_live_owner = [&](std::string_view name,
                                     const Symbol &symbol,
                                     SourceLocation location) {
      if (!report_local_warnings || !symbol.may_be_initialized ||
          deferred_values.contains(std::string{name}) ||
          borrowed_values.contains(std::string{name}) ||
          !potentially_owns_value(symbol.type) ||
          !warned_leak_locations.insert(location.offset).second)
        return;
      result.diagnostics.push_back(Diagnostic{
          DiagnosticSeverity::Warning,
          DiagnosticCode::AnalyzerPotentialMemoryLeak,
          "value '" + std::string{name} + "' has owning type '" +
              symbol.type.name() +
              "' and may reach the end of its scope without being deleted "
              "or moved",
          location,
          {"if ownership was transferred, schedule cleanup with 'defer "
           "delete " +
           std::string{name} +
           "'; borrowed aliases need an ownership-explicit API"},
          {},
          {},
      });
    };
    const auto warn_all_live_owners = [&](const SymbolTable &active) {
      for (const auto &[name, location] : local_declarations) {
        const auto symbol = active.find(name);
        if (symbol != active.end())
          warn_live_owner(name, symbol->second, location);
      }
    };
    if (!is_destructor && owner != nullptr &&
        (context.function->name == "hash" ||
         context.function->name == "equals") &&
        std::any_of(owner->implemented_traits.begin(),
                    owner->implemented_traits.end(),
                    [](const ast::TypeReference &trait) {
                      return trait.name == "Hashing";
                    })) {
      for (const ast::FunctionDeclaration::Parameter &parameter : parameters) {
        borrowed_values.insert(parameter.name);
        transfer_protected_values.insert(parameter.name);
      }
    }
    const bool is_borrowing_enum_observer =
        owner == nullptr &&
        ((context_module == std::optional<std::string>{"std.option"} &&
          (function_name == "isSome" || function_name == "isNone")) ||
         (context_module == std::optional<std::string>{"std.result"} &&
          (function_name == "isOk" || function_name == "isError")));
    if (!is_destructor && is_borrowing_enum_observer) {
      borrowed_values.insert(parameters.front().name);
      transfer_protected_values.insert(parameters.front().name);
    }

    std::function<SemanticType(const ast::Expression &)> expression_type;
    std::function<void(const ast::Expression &, const SemanticType &,
                       SourceLocation)>
        validate_expression;
    const ast::Expression *contextual_expression = nullptr;
    const SemanticType *contextual_expected_type = nullptr;
    const auto speculative_expression_type = [&](const ast::Expression &value) {
      SymbolTable *const active_symbols_before = active_symbols;
      const SymbolTable symbols_before = *active_symbols_before;
      const auto *const active_type_parameters_before = active_type_parameters;
      const auto *const active_type_substitutions_before =
          active_type_substitutions;
      auto *const active_lambda_captures_before = active_lambda_captures;
      const std::optional<std::string> context_module_before = context_module;
      const std::size_t diagnostics_before = result.diagnostics.size();
      const auto warned_leaks_before = warned_leak_locations;
      const auto warned_returns_before = warned_unannotated_return_locations;
      const auto used_locals_before = used_local_declarations;
      const auto captures_before = lambda_captures;
      const auto lambda_locations_before = local_lambda_locations;
      const auto inferred_calls_before = result.inferred_generic_arguments;
      const std::optional<std::unordered_set<std::string>>
          active_captures_before =
              active_lambda_captures_before == nullptr
                  ? std::nullopt
                  : std::optional<std::unordered_set<std::string>>{
                        *active_lambda_captures_before};
      const auto restore = [&] {
        active_symbols = active_symbols_before;
        *active_symbols_before = symbols_before;
        active_type_parameters = active_type_parameters_before;
        active_type_substitutions = active_type_substitutions_before;
        active_lambda_captures = active_lambda_captures_before;
        context_module = context_module_before;
        result.diagnostics.resize(diagnostics_before);
        warned_leak_locations = warned_leaks_before;
        warned_unannotated_return_locations = warned_returns_before;
        used_local_declarations = used_locals_before;
        lambda_captures = captures_before;
        local_lambda_locations = lambda_locations_before;
        result.inferred_generic_arguments = inferred_calls_before;
        if (active_captures_before.has_value())
          *active_lambda_captures_before = *active_captures_before;
      };
      try {
        SemanticType candidate = expression_type(value);
        restore();
        return candidate;
      } catch (...) {
        restore();
        throw;
      }
    };
    const auto is_borrowed_pointer_expression =
        [&](const ast::Expression &expression) {
          if (const auto *identifier =
                  std::get_if<ast::IdentifierExpression>(&expression.value))
            return borrowed_values.contains(identifier->name);
          const auto *call =
              std::get_if<ast::CallExpression>(&expression.value);
          if (call == nullptr)
            return false;
          if (call->callee == "cstr" || call->callee == "stringData" ||
              call->callee == "null")
            return true;
          const auto callee =
              find_in_context(functions, context_module, call->callee);
          return callee != functions.end() && callee->second->is_external &&
                 callee->second->return_ownership ==
                     ast::ReturnOwnership::Borrow;
        };
    const auto apply_external_ownership_contract =
        [&](const ast::FunctionDeclaration &callee,
            const ast::FunctionDeclaration::Parameter &parameter,
            const ast::Expression &argument, const SemanticType &argument_type,
            std::string_view display_name) {
          if (!callee.is_external || (!argument_type.is_pointer() &&
                                      !potentially_owns_value(argument_type)))
            return;
          if (parameter.ownership == ast::ParameterOwnership::Unspecified) {
            emit_warning(
                DiagnosticCode::AnalyzerUnannotatedExternOwnership,
                expression_location(argument),
                "external function '" + std::string{display_name} +
                    "' receives value of type '" + argument_type.name() +
                    "' without a borrow or consume ownership qualifier",
                {"declare this external parameter as borrow or consume"});
            return;
          }
          if (parameter.ownership == ast::ParameterOwnership::Borrow) {
            if (std::holds_alternative<ast::MoveExpression>(argument.value))
              throw CompileError{
                  expression_location(argument),
                  "borrow parameter '" + parameter.name +
                      "' cannot receive an explicit ownership move"};
            if (const auto *call =
                    std::get_if<ast::CallExpression>(&argument.value);
                call != nullptr &&
                (call->callee == "alloc" || call->callee == "realloc"))
              emit_warning(
                  DiagnosticCode::AnalyzerBorrowedTemporaryOwner,
                  expression_location(argument),
                  "borrow parameter '" + parameter.name +
                      "' receives a temporary allocation that is abandoned "
                      "after the external call",
                  {"bind the allocation to a local and release it after the "
                   "call, or declare the parameter consume"});
            return;
          }

          require_guard_transfer_allowed(argument,
                                         expression_location(argument));
          if (is_borrowed_pointer_expression(argument)) {
            const auto *call =
                std::get_if<ast::CallExpression>(&argument.value);
            const std::string expression_name =
                call == nullptr ? "borrowed local" : "'" + call->callee + "'";
            throw CompileError{
                expression_location(argument),
                "consume parameter '" + parameter.name +
                    "' cannot take ownership of borrowed or null pointer "
                    "expression " +
                    expression_name};
          }
          if (const auto *identifier =
                  std::get_if<ast::IdentifierExpression>(&argument.value)) {
            if (borrowed_values.contains(identifier->name))
              throw CompileError{expression_location(argument),
                                 "borrowed value '" + identifier->name +
                                     "' cannot be consumed by external "
                                     "function '" +
                                     std::string{display_name} + "'"};
            if (deferred_values.contains(identifier->name))
              throw CompileError{expression_location(argument),
                                 "owning value '" + identifier->name +
                                     "' is scheduled for deferred cleanup"};
            if (const auto local = active_symbols->find(identifier->name);
                local != active_symbols->end()) {
              local->second.is_initialized = false;
              local->second.may_be_initialized = false;
              return;
            }
            if (visible_global(identifier->name) != nullptr)
              throw CompileError{
                  expression_location(argument),
                  "owning global value '" + identifier->name +
                      "' cannot be consumed by an external function"};
          }
          if (std::holds_alternative<ast::MemberAccessExpression>(
                  argument.value))
            throw CompileError{
                expression_location(argument),
                "consume parameter '" + parameter.name +
                    "' requires an owning local or temporary, not a field"};
        };
    const auto declared_call_type = [&](const ast::FunctionDeclaration &callee,
                                        const std::vector<ast::TypeReference>
                                            &type_arguments,
                                        const std::vector<
                                            std::unique_ptr<ast::Expression>>
                                            &arguments,
                                        SourceLocation location,
                                        std::string_view display_name,
                                        const ast::Expression *expression_key) {
      const bool infer_type_arguments =
          type_arguments.empty() && !callee.type_parameters.empty();
      if (!infer_type_arguments &&
          type_arguments.size() != callee.type_parameters.size())
        throw CompileError{
            location, "function '" + std::string{display_name} + "' expects " +
                          std::to_string(callee.type_parameters.size()) +
                          " type argument(s), got " +
                          std::to_string(type_arguments.size())};
      if ((!callee.is_variadic &&
           arguments.size() != callee.parameters.size()) ||
          (callee.is_variadic && arguments.size() < callee.parameters.size()))
        throw CompileError{
            location, "function '" + std::string{display_name} + "' expects " +
                          (callee.is_variadic ? "at least " : "") +
                          std::to_string(callee.parameters.size()) +
                          " argument(s), got " +
                          std::to_string(arguments.size())};

      std::unordered_map<std::string, SemanticType> substitutions;
      for (std::size_t index = 0; index < type_arguments.size(); ++index)
        substitutions.emplace(callee.type_parameters[index],
                              resolve_type(type_arguments[index],
                                           *active_type_parameters,
                                           &class_arities));
      const std::unordered_set<std::string> callee_parameters{
          callee.type_parameters.begin(), callee.type_parameters.end()};
      if (infer_type_arguments) {
        const auto infer_from_type =
            [&](const auto &self, const SemanticType &pattern,
                const SemanticType &candidate) -> void {
          const bool type_parameter =
              !pattern.is_concrete() && !pattern.is_class() &&
              !pattern.is_pointer() && !pattern.is_enum() &&
              !pattern.is_function() &&
              callee_parameters.contains(pattern.parameter);
          if (type_parameter) {
            const auto [existing, inserted] =
                substitutions.emplace(pattern.parameter, candidate);
            if (!inserted && !same_type(existing->second, candidate))
              throw CompileError{
                  location, "generic type parameter '" + pattern.parameter +
                                "' has incompatible argument types"};
            return;
          }
          const bool same_outer_type =
              pattern.concrete == candidate.concrete &&
              pattern.parameter == candidate.parameter &&
              pattern.class_type == candidate.class_type &&
              pattern.pointer_type == candidate.pointer_type &&
              pattern.enum_type == candidate.enum_type &&
              pattern.function_type == candidate.function_type &&
              pattern.type_arguments.size() == candidate.type_arguments.size();
          if (!same_outer_type)
            return;
          for (std::size_t index = 0; index < pattern.type_arguments.size();
               ++index)
            self(self, pattern.type_arguments[index],
                 candidate.type_arguments[index]);
        };
        for (std::size_t index = 0;
             index < arguments.size() && index < callee.parameters.size();
             ++index) {
          const SemanticType pattern = resolve_type(
              callee.parameters[index].type, callee_parameters, &class_arities);
          const SemanticType candidate =
              speculative_expression_type(*arguments[index]);
          infer_from_type(infer_from_type, pattern, candidate);
        }
        if (contextual_expression == expression_key &&
            contextual_expected_type != nullptr) {
          const SemanticType return_pattern = resolve_type(
              callee.return_type, callee_parameters, &class_arities);
          infer_from_type(infer_from_type, return_pattern,
                          *contextual_expected_type);
        }
        for (const std::string &parameter : callee.type_parameters)
          if (!substitutions.contains(parameter))
            throw CompileError{location,
                               "generic type parameter '" + parameter +
                                   "' is not constrained by call arguments; "
                                   "help: add explicit type arguments"};
      }
      for (const ast::TypeConstraint &constraint : callee.type_constraints) {
        const SemanticType &candidate = substitutions.at(constraint.parameter);
        if (const auto kind = derivation_constraint(constraint.trait.name);
            kind.has_value() && constraint.trait.type_arguments.empty()) {
          const bool satisfies = *kind == ast::DerivationKind::Copy
                                     ? satisfies_copy(candidate)
                                     : supports_derivation(candidate, *kind);
          if (!satisfies)
            throw CompileError{location, "type '" + candidate.name() +
                                             "' does not satisfy constraint '" +
                                             constraint.trait.name +
                                             "' for "
                                             "type parameter '" +
                                             constraint.parameter + "'"};
          continue;
        }
        TraitInstance requirement =
            resolve_trait(constraint.trait, callee_parameters);
        for (SemanticType &argument : requirement.type_arguments)
          argument = substitute(std::move(argument), substitutions);
        if (!satisfies_active_trait(candidate, requirement))
          throw CompileError{location, "type '" + candidate.name() +
                                           "' does not satisfy constraint '" +
                                           requirement.declaration->name +
                                           "' for type parameter '" +
                                           constraint.parameter + "'"};
      }
      for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index >= callee.parameters.size()) {
          const SemanticType argument_type = expression_type(*arguments[index]);
          if (!is_c_variadic_type(argument_type))
            throw CompileError{expression_location(*arguments[index]),
                               "variadic C argument has incompatible type '" +
                                   argument_type.name() + "'"};
          if (callee.is_external &&
              !is_borrowed_pointer_expression(*arguments[index]) &&
              (argument_type.is_pointer() ||
               potentially_owns_value(argument_type)))
            emit_warning(DiagnosticCode::AnalyzerUnannotatedExternOwnership,
                         expression_location(*arguments[index]),
                         "external variadic call receives value of type '" +
                             argument_type.name() +
                             "' without a verifiable ownership contract",
                         {"document whether native code borrows, retains, or "
                          "releases this value"});
          continue;
        }
        SemanticType expected = resolve_type(callee.parameters[index].type,
                                             callee_parameters, &class_arities);
        expected = substitute(std::move(expected), substitutions);
        const bool observes_owned_enum =
            index == 0 &&
            ((callee.module_name == std::optional<std::string>{"std.option"} &&
              (callee.name == "isSome" || callee.name == "isNone")) ||
             (callee.module_name == std::optional<std::string>{"std.result"} &&
              (callee.name == "isOk" || callee.name == "isError")));
        const bool previous_contextual_borrow_expression =
            contextual_borrow_expression;
        const std::string_view previous_contextual_borrow_enum_name =
            contextual_borrow_enum_name;
        contextual_borrow_expression =
            observes_owned_enum || callee.parameters[index].ownership ==
                                       ast::ParameterOwnership::Borrow;
        contextual_borrow_enum_name =
            !observes_owned_enum
                ? std::string_view{}
                : (callee.module_name ==
                           std::optional<std::string>{"std.option"}
                       ? std::string_view{"Option"}
                       : std::string_view{"Result"});
        validate_expression(*arguments[index], expected,
                            expression_location(*arguments[index]));
        apply_external_ownership_contract(callee, callee.parameters[index],
                                          *arguments[index], expected,
                                          display_name);
        contextual_borrow_expression = previous_contextual_borrow_expression;
        contextual_borrow_enum_name = previous_contextual_borrow_enum_name;
      }
      if (infer_type_arguments) {
        std::vector<SemanticType> inferred;
        inferred.reserve(callee.type_parameters.size());
        for (const std::string &parameter : callee.type_parameters)
          inferred.push_back(substitutions.at(parameter));
        result.inferred_generic_arguments.insert_or_assign(expression_key,
                                                           std::move(inferred));
      }
      SemanticType call_return = substitute(
          resolve_type(callee.return_type, callee_parameters, &class_arities),
          substitutions);
      if (callee.is_external && call_return.is_pointer() &&
          callee.return_ownership == ast::ReturnOwnership::Unspecified &&
          warned_unannotated_return_locations.insert(location.offset).second)
        emit_warning(
            DiagnosticCode::AnalyzerUnannotatedExternReturn, location,
            "external function '" + std::string{display_name} +
                "' returns a pointer without a borrow or owned ownership "
                "qualifier",
            {"declare this external return as borrow or owned"});
      return call_return;
    };
    const auto class_substitutions =
        [](const ast::ClassDeclaration &class_declaration,
           const SemanticType &instance) {
          std::unordered_map<std::string, SemanticType> substitutions;
          for (std::size_t index = 0;
               index < class_declaration.type_parameters.size(); ++index) {
            substitutions.emplace(class_declaration.type_parameters[index],
                                  instance.type_arguments[index]);
          }
          return substitutions;
        };

    validate_expression = [&](const ast::Expression &expression,
                              const SemanticType &expected,
                              SourceLocation location) {
      if (expected.is_concrete() &&
          accepts_contextual_integer_literal(*expected.concrete)) {
        if (integer_literal_value(expression)) {
          if (integer_literal_fits(expression, *expected.concrete))
            return;
          throw CompileError{expression_location(expression),
                             "integer literal is outside the " +
                                 integer_range_description(*expected.concrete)};
        }
      }

      const ast::Expression *previous_contextual_expression =
          contextual_expression;
      const SemanticType *previous_contextual_expected_type =
          contextual_expected_type;
      contextual_expression = &expression;
      contextual_expected_type = &expected;
      SemanticType actual;
      try {
        actual = expression_type(expression);
      } catch (...) {
        contextual_expression = previous_contextual_expression;
        contextual_expected_type = previous_contextual_expected_type;
        throw;
      }
      contextual_expression = previous_contextual_expression;
      contextual_expected_type = previous_contextual_expected_type;
      if (same_type(actual, expected)) {
        if (contextual_borrow_expression && !actual.is_pointer() &&
            aggregate_owns_value(actual) &&
            !std::holds_alternative<ast::IdentifierExpression>(
                expression.value))
          throw CompileError{
              location, contextual_borrow_enum_name.empty()
                            ? "borrowing an owning value of type '" +
                                  actual.name() + "' requires a local value"
                            : "observing an owning " +
                                  std::string{contextual_borrow_enum_name} +
                                  " requires a local value"};
        if (const auto *identifier =
                std::get_if<ast::IdentifierExpression>(&expression.value);
            identifier != nullptr &&
            borrowed_values.contains(identifier->name) &&
            potentially_owns_value(expected) && !contextual_borrow_expression &&
            !contextual_borrow_pointer_expression)
          throw CompileError{location,
                             "borrowed value '" + identifier->name +
                                 "' cannot be passed to an owning parameter"};
        if ((actual.is_enum() ||
             (actual.is_class() &&
              classes.at(actual.parameter)->is_value_type)) &&
            aggregate_owns_value(actual)) {
          if (std::holds_alternative<ast::IdentifierExpression>(
                  expression.value) &&
              !contextual_borrow_expression)
            throw CompileError{location, "transferring owning aggregate '" +
                                             actual.name() +
                                             "' requires an explicit move"};
          if (std::holds_alternative<ast::MemberAccessExpression>(
                  expression.value))
            throw CompileError{
                location, "an owning aggregate field cannot be transferred "
                          "independently; move its enclosing aggregate"};
        }
        return;
      }

      throw CompileError{location, "cannot use expression of type '" +
                                       actual.name() + "' where type '" +
                                       expected.name() + "' is required"};
    };
    const auto validate_return_expression =
        [&](const ast::ReturnStatement &return_statement) {
          const ast::Expression &expression = *return_statement.expression;
          if (return_type.is_concrete() &&
              accepts_contextual_integer_literal(*return_type.concrete)) {
            if (integer_literal_value(expression)) {
              if (integer_literal_fits(expression, *return_type.concrete))
                return;
              throw CompileError{
                  expression_location(expression),
                  "integer literal is outside the " +
                      integer_range_description(*return_type.concrete)};
            }
          }

          const ast::Expression *previous_contextual_expression =
              contextual_expression;
          const SemanticType *previous_contextual_expected_type =
              contextual_expected_type;
          contextual_expression = &expression;
          contextual_expected_type = &return_type;
          SemanticType actual;
          try {
            actual = expression_type(expression);
          } catch (...) {
            contextual_expression = previous_contextual_expression;
            contextual_expected_type = previous_contextual_expected_type;
            throw;
          }
          contextual_expression = previous_contextual_expression;
          contextual_expected_type = previous_contextual_expected_type;
          if (same_type(actual, return_type)) {
            if ((actual.is_enum() ||
                 (actual.is_class() &&
                  classes.at(actual.parameter)->is_value_type)) &&
                aggregate_owns_value(actual)) {
              if (std::holds_alternative<ast::IdentifierExpression>(
                      expression.value))
                throw CompileError{return_statement.location,
                                   "returning owning aggregate '" +
                                       actual.name() +
                                       "' requires an explicit move"};
              if (std::holds_alternative<ast::MemberAccessExpression>(
                      expression.value))
                throw CompileError{
                    return_statement.location,
                    "an owning aggregate field cannot be returned "
                    "independently; move its enclosing aggregate"};
            }
            return;
          }

          throw CompileError{
              return_statement.location,
              "cannot return expression of type '" + actual.name() +
                  "' from function '" + function_name + "'; expected '" +
                  return_type.name() + "', received '" + actual.name() + "'"};
        };

    expression_type = [&](const ast::Expression &expression) -> SemanticType {
      return std::visit(
          [&](const auto &node) -> SemanticType {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, ast::IntegerLiteralExpression>) {
              if (!integer_literal_fits(expression, Type::int_type()))
                throw CompileError{
                    node.location,
                    "integer literal is outside the signed 32-bit range"};
              return SemanticType{&Type::int_type(), {}};
            } else if constexpr (std::is_same_v<Node,
                                                ast::DoubleLiteralExpression>) {
              return SemanticType{node.is_float ? &Type::float_type()
                                                : &Type::double_type(),
                                  {}};
            } else if constexpr (std::is_same_v<
                                     Node, ast::CharacterLiteralExpression>) {
              return SemanticType{&Type::char_type(), {}};
            } else if constexpr (std::is_same_v<
                                     Node, ast::BooleanLiteralExpression>) {
              return SemanticType{&Type::bool_type(), {}};
            } else if constexpr (std::is_same_v<Node,
                                                ast::StringLiteralExpression>) {
              return SemanticType{&Type::string_type(), {}};
            } else if constexpr (std::is_same_v<Node,
                                                ast::ArrayLiteralExpression>) {
              const auto array_class = classes.find("std.array.Array");
              if (array_class == classes.end())
                throw CompileError{
                    DiagnosticCode::AnalyzerInvalidArrayLiteral, node.location,
                    "array literal requires Array[T]; help: import std.array"};
              const auto contextual_array_class =
                  contextual_expected_type == nullptr
                      ? classes.end()
                      : classes.find(contextual_expected_type->parameter);
              const bool has_contextual_array_type =
                  contextual_expression == &expression &&
                  contextual_expected_type != nullptr &&
                  contextual_expected_type->is_class() &&
                  contextual_array_class != classes.end() &&
                  contextual_array_class->second->module_name ==
                      std::optional<std::string>{"std.array"} &&
                  contextual_array_class->second->name == "Array" &&
                  contextual_expected_type->type_arguments.size() == 1;
              SemanticType element_type;
              if (has_contextual_array_type) {
                element_type = contextual_expected_type->type_arguments.front();
              } else {
                if (node.elements.empty())
                  throw CompileError{
                      DiagnosticCode::AnalyzerInvalidArrayLiteral,
                      node.location,
                      "empty array literal requires an explicit Array[T] type"};
                element_type = expression_type(*node.elements.front());
              }
              for (const auto &element : node.elements) {
                if (aggregate_owns_value(element_type) &&
                    std::holds_alternative<ast::IdentifierExpression>(
                        element->value))
                  throw CompileError{
                      DiagnosticCode::AnalyzerInvalidArrayLiteral,
                      expression_location(*element),
                      "owning array literal element requires an explicit move"};
                try {
                  validate_expression(*element, element_type, node.location);
                } catch (const CompileError &error) {
                  throw CompileError{
                      DiagnosticCode::AnalyzerInvalidArrayLiteral,
                      expression_location(*element), error.what()};
                }
              }
              result.inferred_generic_arguments.insert_or_assign(
                  &expression, std::vector<SemanticType>{element_type});
              if (has_contextual_array_type)
                return *contextual_expected_type;
              return SemanticType{
                  nullptr, array_class->first, true, {element_type}};
            } else if constexpr (std::is_same_v<Node,
                                                ast::IdentifierExpression>) {
              const auto iterator = active_symbols->find(node.name);
              if (iterator == active_symbols->end()) {
                if (const Symbol *global = visible_global(node.name))
                  return global->type;
                throw CompileError{DiagnosticCode::AnalyzerUnknownValue,
                                   node.location,
                                   "unknown value '" + node.name + "'"};
              }
              if (!iterator->second.is_initialized) {
                throw CompileError{node.location,
                                   "variable '" + node.name +
                                       "' is used before initialization"};
              }
              if (const auto declaration = local_declarations.find(node.name);
                  declaration != local_declarations.end()) {
                used_local_declarations.insert(declaration->second.offset);
                if (active_lambda_captures != nullptr)
                  active_lambda_captures->insert(node.name);
              }
              return iterator->second.type;
            } else if constexpr (std::is_same_v<Node, ast::LambdaExpression>) {
              SymbolTable lambda_symbols = *active_symbols;
              std::unordered_set<std::string> parameter_names;
              const auto previous_borrowed_values = borrowed_values;
              std::vector<SemanticType> signature;
              signature.reserve(node.parameters.size() + 1);
              for (const ast::LambdaExpression::Parameter &parameter :
                   node.parameters) {
                if (!parameter_names.insert(parameter.name).second)
                  throw CompileError{parameter.location,
                                     "lambda parameter '" + parameter.name +
                                         "' is already declared"};
                SemanticType parameter_type = resolve_type(
                    parameter.type, *active_type_parameters, &class_arities);
                if (active_type_substitutions != nullptr)
                  parameter_type = substitute(std::move(parameter_type),
                                              *active_type_substitutions);
                if (parameter_type.is_concrete() &&
                    parameter_type.concrete->kind() == TypeKind::Unit)
                  throw CompileError{
                      parameter.location,
                      "Unit cannot be used as a lambda parameter type"};
                signature.push_back(parameter_type);
                lambda_symbols.insert_or_assign(
                    parameter.name, Symbol{parameter_type, false, true});
                if (contextual_borrow_lambda_parameters) {
                  borrowed_values.insert(parameter.name);
                }
              }
              SymbolTable *previous_symbols = active_symbols;
              active_symbols = &lambda_symbols;
              const bool previous_inside_lambda = inside_lambda;
              std::unordered_set<std::string> captures;
              std::unordered_set<std::string> *previous_lambda_captures =
                  active_lambda_captures;
              active_lambda_captures = &captures;
              const auto previous_transfer_protected =
                  transfer_protected_values;
              for (const auto &[name, symbol] : *previous_symbols) {
                static_cast<void>(symbol);
                if (!parameter_names.contains(name))
                  transfer_protected_values.insert(name);
              }
              inside_lambda = true;
              signature.push_back(expression_type(*node.body));
              inside_lambda = previous_inside_lambda;
              active_lambda_captures = previous_lambda_captures;
              for (const std::string &parameter : parameter_names)
                captures.erase(parameter);
              lambda_captures.insert_or_assign(
                  node.location.offset,
                  std::vector<std::string>{captures.begin(), captures.end()});
              transfer_protected_values = previous_transfer_protected;
              borrowed_values = previous_borrowed_values;
              active_symbols = previous_symbols;
              return SemanticType{
                  nullptr, "Function", false, std::move(signature),
                  false,   false,      true};
            } else if constexpr (std::is_same_v<Node, ast::CallExpression>) {
              const Symbol *callable = nullptr;
              if (const auto local = active_symbols->find(node.callee);
                  local != active_symbols->end())
                callable = &local->second;
              else
                callable = visible_global(node.callee);
              if (callable != nullptr) {
                if (!callable->is_initialized)
                  throw CompileError{node.location,
                                     "function value '" + node.callee +
                                         "' is used before initialization"};
                if (!callable->type.is_function())
                  throw CompileError{node.location, "value '" + node.callee +
                                                        "' is not callable"};
                if (!node.type_arguments.empty())
                  throw CompileError{
                      node.location,
                      "a function value does not accept type arguments"};
                const std::vector<SemanticType> &signature =
                    callable->type.type_arguments;
                const std::size_t parameter_count = signature.size() - 1;
                if (node.arguments.size() != parameter_count)
                  throw CompileError{node.location,
                                     "function value '" + node.callee +
                                         "' expects " +
                                         std::to_string(parameter_count) +
                                         " argument(s), got " +
                                         std::to_string(node.arguments.size())};
                for (std::size_t index = 0; index < parameter_count; ++index)
                  validate_expression(
                      *node.arguments[index], signature[index],
                      expression_location(*node.arguments[index]));
                return signature.back();
              }
              if (node.callee == "print" || node.callee == "println") {
                if (!node.type_arguments.empty() || node.arguments.size() != 1)
                  throw CompileError{node.location,
                                     node.callee +
                                         " expects one printable argument and "
                                         "no type argument"};
                const SemanticType argument =
                    expression_type(*node.arguments.front());
                if (!argument.is_concrete() ||
                    (argument.concrete->kind() != TypeKind::Int &&
                     argument.concrete->kind() != TypeKind::UInt &&
                     argument.concrete->kind() != TypeKind::Long &&
                     argument.concrete->kind() != TypeKind::ULong &&
                     argument.concrete->kind() != TypeKind::Float &&
                     argument.concrete->kind() != TypeKind::Double &&
                     argument.concrete->kind() != TypeKind::Byte &&
                     argument.concrete->kind() != TypeKind::UByte &&
                     argument.concrete->kind() != TypeKind::Short &&
                     argument.concrete->kind() != TypeKind::UShort &&
                     argument.concrete->kind() != TypeKind::Char &&
                     argument.concrete->kind() != TypeKind::Bool &&
                     argument.concrete->kind() != TypeKind::String &&
                     argument.concrete->kind() != TypeKind::ISize &&
                     argument.concrete->kind() != TypeKind::USize))
                  throw CompileError{
                      node.location,
                      node.callee +
                          " supports int, double, byte, char, bool, string, "
                          "and "
                          "usize values, plus the other numeric primitives"};
                return SemanticType{&Type::unit_type()};
              }
              if (node.callee == "panic") {
                if (!node.type_arguments.empty() || node.arguments.size() != 1)
                  throw CompileError{
                      node.location,
                      "panic expects one string argument and no type argument"};
                validate_expression(
                    *node.arguments.front(), SemanticType{&Type::string_type()},
                    expression_location(*node.arguments.front()));
                return SemanticType{&Type::unit_type()};
              }
              if (node.callee == "cstr") {
                if (!node.type_arguments.empty() || node.arguments.size() != 1)
                  throw CompileError{
                      node.location,
                      "cstr expects one string argument and no type argument"};
                validate_expression(
                    *node.arguments.front(), SemanticType{&Type::string_type()},
                    expression_location(*node.arguments.front()));
                return SemanticType{nullptr,
                                    "Ptr",
                                    false,
                                    {SemanticType{&Type::byte_type()}},
                                    true};
              }
              if (node.callee == "stringData" ||
                  node.callee == "stringLength") {
                if (!node.type_arguments.empty() || node.arguments.size() != 1)
                  throw CompileError{node.location,
                                     node.callee +
                                         " expects one string argument"};
                validate_expression(
                    *node.arguments.front(), SemanticType{&Type::string_type()},
                    expression_location(*node.arguments.front()));
                if (node.callee == "stringLength")
                  return SemanticType{&Type::usize_type()};
                return SemanticType{nullptr,
                                    "Ptr",
                                    false,
                                    {SemanticType{&Type::byte_type()}},
                                    true};
              }
              if (node.callee == "stringView") {
                if (!node.type_arguments.empty() || node.arguments.size() != 2)
                  throw CompileError{node.location,
                                     "stringView expects a byte pointer and "
                                     "byte length"};
                const SemanticType byte_pointer{
                    nullptr,
                    "Ptr",
                    false,
                    {SemanticType{&Type::byte_type()}},
                    true};
                const bool previous_contextual_borrow_pointer_expression =
                    contextual_borrow_pointer_expression;
                contextual_borrow_pointer_expression = true;
                validate_expression(*node.arguments[0], byte_pointer,
                                    expression_location(*node.arguments[0]));
                contextual_borrow_pointer_expression =
                    previous_contextual_borrow_pointer_expression;
                validate_expression(*node.arguments[1],
                                    SemanticType{&Type::usize_type()},
                                    expression_location(*node.arguments[1]));
                return SemanticType{&Type::string_type()};
              }
              if (node.callee == "debug") {
                if (!node.type_arguments.empty() || node.arguments.size() != 1)
                  throw CompileError{
                      node.location,
                      "debug expects one value argument and no type argument"};
                const SemanticType candidate =
                    expression_type(*node.arguments.front());
                if (!supports_derivation(candidate, ast::DerivationKind::Debug))
                  throw CompileError{node.location,
                                     "type '" + candidate.name() +
                                         "' does not derive Debug"};
                return SemanticType{&Type::unit_type()};
              }
              if (node.callee == "__derivedHash" ||
                  node.callee == "__derivedEquals") {
                const std::size_t value_arguments =
                    node.callee == "__derivedHash" ? 1 : 2;
                if (node.type_arguments.size() != 1 ||
                    node.arguments.size() != value_arguments)
                  throw CompileError{node.location,
                                     node.callee +
                                         " expects one type argument and " +
                                         std::to_string(value_arguments) +
                                         " value argument(s)"};
                SemanticType candidate =
                    resolve_type(node.type_arguments.front(),
                                 *active_type_parameters, &class_arities);
                if (active_type_substitutions != nullptr)
                  candidate = substitute(std::move(candidate),
                                         *active_type_substitutions);
                const ast::DerivationKind capability =
                    node.callee == "__derivedHash"
                        ? ast::DerivationKind::Hashing
                        : ast::DerivationKind::Equality;
                if (!supports_derivation(candidate, capability))
                  throw CompileError{
                      node.location,
                      "type '" + candidate.name() + "' does not derive " +
                          std::string{derivation_name(capability)}};
                const bool previous_contextual_borrow_expression =
                    contextual_borrow_expression;
                contextual_borrow_expression = true;
                for (const auto &argument : node.arguments)
                  validate_expression(*argument, candidate,
                                      expression_location(*argument));
                contextual_borrow_expression =
                    previous_contextual_borrow_expression;
                return node.callee == "__derivedHash"
                           ? SemanticType{&Type::usize_type()}
                           : SemanticType{&Type::bool_type()};
              }
              if (node.callee == "alloc" || node.callee == "realloc" ||
                  node.callee == "reallocPreserving" || node.callee == "null" ||
                  node.callee == "sizeof" || node.callee == "alignof") {
                if (node.type_arguments.size() != 1)
                  throw CompileError{node.location,
                                     "memory intrinsic '" + node.callee +
                                         "' expects exactly one type argument"};
                SemanticType element_type =
                    resolve_type(node.type_arguments.front(),
                                 *active_type_parameters, &class_arities);
                if (active_type_substitutions != nullptr)
                  element_type = substitute(std::move(element_type),
                                            *active_type_substitutions);
                if (element_type.is_concrete() &&
                    element_type.concrete->kind() == TypeKind::Unit)
                  throw CompileError{node.location,
                                     "memory intrinsics cannot use Unit"};
                SemanticType pointer_type{
                    nullptr, "Ptr", false, {element_type}, true};
                if (node.callee == "null") {
                  if (!node.arguments.empty())
                    throw CompileError{node.location,
                                       "null expects no value argument"};
                  return pointer_type;
                }
                if (node.callee == "sizeof" || node.callee == "alignof") {
                  if (!node.arguments.empty())
                    throw CompileError{node.location,
                                       node.callee +
                                           " expects no value argument"};
                  return SemanticType{&Type::usize_type()};
                }
                const std::size_t expected_arguments =
                    node.callee == "alloc" ? 1 : 2;
                if (node.arguments.size() != expected_arguments)
                  throw CompileError{
                      node.location,
                      "memory intrinsic '" + node.callee + "' expects " +
                          std::to_string(expected_arguments) + " argument(s)"};
                std::size_t count_index = 0;
                if (node.callee == "realloc" ||
                    node.callee == "reallocPreserving") {
                  validate_expression(*node.arguments[0], pointer_type,
                                      expression_location(*node.arguments[0]));
                  if (node.callee == "realloc" &&
                      potentially_owns_value(element_type))
                    emit_warning(
                        DiagnosticCode::AnalyzerOwningBufferReallocated,
                        node.location,
                        "realloc may relocate or discard elements of owning "
                        "type '" +
                            element_type.name() + "' without destroying them",
                        {"move or destroy live elements explicitly before "
                         "resizing their raw buffer"});
                  count_index = 1;
                }
                validate_expression(
                    *node.arguments[count_index],
                    SemanticType{&Type::usize_type()},
                    expression_location(*node.arguments[count_index]));
                return pointer_type;
              }
              if (node.callee == "adoptReallocation") {
                if (node.type_arguments.size() != 1 ||
                    node.arguments.size() != 2)
                  throw CompileError{
                      node.location,
                      "adoptReallocation expects one type argument and two "
                      "pointer arguments"};
                const SemanticType element_type =
                    resolve_type(node.type_arguments.front(),
                                 *active_type_parameters, &class_arities);
                const SemanticType pointer_type{
                    nullptr, "Ptr", false, {element_type}, true};
                for (const auto &argument : node.arguments) {
                  require_guard_transfer_allowed(
                      *argument, expression_location(*argument));
                  if (is_borrowed_pointer_expression(*argument))
                    throw CompileError{
                        expression_location(*argument),
                        "borrowed pointer cannot be adopted after realloc"};
                  validate_expression(*argument, pointer_type,
                                      expression_location(*argument));
                  if (const auto *identifier =
                          std::get_if<ast::IdentifierExpression>(
                              &argument->value);
                      identifier != nullptr &&
                      active_symbols->contains(identifier->name)) {
                    active_symbols->at(identifier->name).is_initialized = false;
                    active_symbols->at(identifier->name).may_be_initialized =
                        false;
                  }
                }
                return pointer_type;
              }
              if (node.callee == "owningCapture") {
                if (node.type_arguments.size() != 1 ||
                    node.arguments.size() != 2)
                  throw CompileError{
                      node.location,
                      "owningCapture expects one owner type argument, an "
                      "owner, and a zero-argument closure"};
                const auto *owner_identifier =
                    std::get_if<ast::IdentifierExpression>(
                        &node.arguments[0]->value);
                require_guard_transfer_allowed(
                    *node.arguments[0],
                    expression_location(*node.arguments[0]));
                if (owner_identifier == nullptr ||
                    !active_symbols->contains(owner_identifier->name))
                  throw CompileError{
                      expression_location(*node.arguments[0]),
                      "owningCapture requires a local owner identifier"};
                if (borrowed_values.contains(owner_identifier->name))
                  throw CompileError{
                      expression_location(*node.arguments[0]),
                      "borrowed value cannot be transferred to a closure"};
                if (deferred_values.contains(owner_identifier->name))
                  throw CompileError{
                      expression_location(*node.arguments[0]),
                      "deferred value cannot be transferred to a closure"};
                SemanticType owner_type =
                    resolve_type(node.type_arguments.front(),
                                 *active_type_parameters, &class_arities);
                if (active_type_substitutions != nullptr)
                  owner_type = substitute(std::move(owner_type),
                                          *active_type_substitutions);
                validate_expression(*node.arguments[0], owner_type,
                                    expression_location(*node.arguments[0]));
                const SemanticType closure_type =
                    expression_type(*node.arguments[1]);
                if (!closure_type.is_function() ||
                    closure_type.type_arguments.size() != 1)
                  throw CompileError{
                      expression_location(*node.arguments[1]),
                      "owningCapture requires a zero-argument closure"};
                const auto *lambda = std::get_if<ast::LambdaExpression>(
                    &node.arguments[1]->value);
                const auto captures =
                    lambda == nullptr
                        ? lambda_captures.end()
                        : lambda_captures.find(lambda->location.offset);
                if (captures == lambda_captures.end() ||
                    std::find(captures->second.begin(), captures->second.end(),
                              owner_identifier->name) == captures->second.end())
                  throw CompileError{
                      expression_location(*node.arguments[1]),
                      "owningCapture closure must capture owner '" +
                          owner_identifier->name + "'"};
                active_symbols->at(owner_identifier->name).is_initialized =
                    false;
                active_symbols->at(owner_identifier->name).may_be_initialized =
                    false;
                return closure_type;
              }
              if (node.callee == "free" || node.callee == "freeStorage") {
                if (!node.type_arguments.empty() || node.arguments.size() != 1)
                  throw CompileError{
                      node.location,
                      "free expects one pointer argument and no type argument"};
                require_guard_transfer_allowed(
                    *node.arguments.front(),
                    expression_location(*node.arguments.front()));
                if (is_borrowed_pointer_expression(*node.arguments.front()))
                  throw CompileError{
                      node.location,
                      "borrowed pointer cannot be released with free"};
                const SemanticType pointer =
                    expression_type(*node.arguments.front());
                if (!pointer.is_pointer())
                  throw CompileError{node.location,
                                     "free requires a Ptr[T] argument"};
                const SemanticType &element_type =
                    pointer.type_arguments.front();
                if (node.callee == "free" &&
                    potentially_owns_value(element_type))
                  emit_warning(
                      DiagnosticCode::AnalyzerOwningBufferFreedWithoutCleanup,
                      node.location,
                      "free releases storage for owning elements of type '" +
                          element_type.name() +
                          "' without running their cleanup",
                      {"destroy or move every initialized element before "
                       "freeing the raw buffer"});
                if (const auto *identifier =
                        std::get_if<ast::IdentifierExpression>(
                            &node.arguments.front()->value)) {
                  require_no_live_borrow(identifier->name, node.location);
                  if (deferred_values.contains(identifier->name))
                    throw CompileError{
                        node.location,
                        "owning value '" + identifier->name +
                            "' is scheduled for deferred cleanup"};
                  if (active_symbols->contains(identifier->name)) {
                    active_symbols->at(identifier->name).is_initialized = false;
                    active_symbols->at(identifier->name).may_be_initialized =
                        false;
                  }
                }
                return SemanticType{&Type::unit_type()};
              }
              if (node.callee == "numericCast" ||
                  node.callee == "checkedCast" ||
                  node.callee == "saturatingCast" ||
                  node.callee == "truncatingCast") {
                const std::string policy = node.callee;
                if (node.type_arguments.size() != 1 ||
                    node.arguments.size() != 1)
                  throw CompileError{
                      node.location,
                      policy + " expects one destination type and one "
                               "value argument"};
                SemanticType destination_type = resolve_type(
                    node.type_arguments.front(), *active_type_parameters,
                    &class_arities, context_module, &scoped_type_aliases);
                if (active_type_substitutions != nullptr)
                  destination_type = substitute(std::move(destination_type),
                                                *active_type_substitutions);
                const SemanticType source_type =
                    expression_type(*node.arguments.front());
                const bool source_numeric =
                    source_type.is_concrete() &&
                    (source_type.concrete->is_integer() ||
                     source_type.concrete->is_floating_point());
                if (!destination_type.is_concrete())
                  throw CompileError{
                      node.location,
                      policy + " requires a concrete numeric destination type"};
                const bool destination_numeric =
                    destination_type.concrete->is_integer() ||
                    destination_type.concrete->is_floating_point();
                if (!source_numeric || !destination_numeric ||
                    (destination_type.is_concrete() &&
                     !can_explicitly_cast(source_type, destination_type)))
                  throw CompileError{
                      node.location,
                      policy == "numericCast"
                          ? "numericCast requires compatible numeric source "
                            "and destination types"
                          : policy + " requires numeric source and "
                                     "destination types"};
                if (policy == "checkedCast") {
                  const auto error_iterator = find_in_context(
                      enums, context_module, "NumericCastError");
                  const auto result_iterator =
                      find_in_context(enums, context_module, "Result");
                  const auto visible_type_name =
                      [&](const ast::EnumDeclaration &declaration) {
                        if (declaration.module_name == context_module)
                          return declaration.name;
                        for (const ast::ImportDeclaration &import :
                             program.imports) {
                          if (import.importing_module != context_module ||
                              import.module_name != declaration.module_name)
                            continue;
                          for (const ast::ImportDeclaration::Symbol &symbol :
                               import.symbols)
                            if (symbol.name == declaration.name)
                              return symbol.alias.value_or(symbol.name);
                          if (import.module_alias.has_value())
                            return *import.module_alias + "." +
                                   declaration.name;
                          if (!import.is_qualified() && !import.is_selective())
                            return declaration.name;
                        }
                        return declaration.name;
                      };
                  const std::string error_name =
                      error_iterator == enums.end()
                          ? "NumericCastError"
                          : visible_type_name(*error_iterator->second);
                  const std::string result_name =
                      result_iterator == enums.end()
                          ? "Result"
                          : visible_type_name(*result_iterator->second);
                  ast::TypeReference error_type{error_name, node.location, {}};
                  ast::TypeReference result_type{
                      result_name,
                      node.location,
                      {node.type_arguments.front(), error_type}};
                  SemanticType resolved_result = resolve_type(
                      result_type, *active_type_parameters, &class_arities,
                      context_module, &scoped_type_aliases);

                  const auto find_case = [](const ast::EnumDeclaration &type,
                                            std::string_view name) {
                    return std::find_if(
                        type.cases.begin(), type.cases.end(),
                        [&](const ast::EnumDeclaration::Case &item) {
                          return item.name == name;
                        });
                  };
                  constexpr std::array<std::string_view, 6> error_cases{
                      "Overflow",  "Underflow",      "IncompatibleSign",
                      "NonFinite", "FractionalLoss", "PrecisionLoss"};
                  if (error_iterator == enums.end() ||
                      error_iterator->second->type_parameters.size() != 0 ||
                      std::any_of(
                          error_cases.begin(), error_cases.end(),
                          [&](std::string_view name) {
                            const auto item =
                                find_case(*error_iterator->second, name);
                            return item ==
                                       error_iterator->second->cases.end() ||
                                   !item->payload_types.empty();
                          }))
                    throw CompileError{
                        node.location,
                        "checkedCast requires NumericCastError to define "
                        "payload-free Overflow, Underflow, IncompatibleSign, "
                        "NonFinite, FractionalLoss and PrecisionLoss cases"};

                  bool valid_result =
                      result_iterator != enums.end() &&
                      result_iterator->second->type_parameters.size() == 2;
                  if (valid_result) {
                    const ast::EnumDeclaration &declaration =
                        *result_iterator->second;
                    const auto ok = find_case(declaration, "Ok");
                    const auto error = find_case(declaration, "Error");
                    valid_result =
                        ok != declaration.cases.end() &&
                        error != declaration.cases.end() &&
                        ok->payload_types.size() == 1 &&
                        error->payload_types.size() == 1 &&
                        ok->payload_types.front().name ==
                            declaration.type_parameters[0] &&
                        ok->payload_types.front().type_arguments.empty() &&
                        error->payload_types.front().name ==
                            declaration.type_parameters[1] &&
                        error->payload_types.front().type_arguments.empty();
                  }
                  if (!valid_result)
                    throw CompileError{
                        node.location,
                        "checkedCast requires Result to define Ok(T) and "
                        "Error(E)"};
                  return resolved_result;
                }
                return destination_type;
              }
              const bool is_builtin_cast = builtin_type(node.callee) != nullptr;
              const bool is_reference_cast =
                  node.callee == "Ptr" ||
                  find_in_context(classes, context_module, node.callee) !=
                      classes.end();
              const bool is_enum_cast =
                  find_in_context(enums, context_module, node.callee) !=
                  enums.end();
              if (is_builtin_cast || is_reference_cast || is_enum_cast) {
                const SemanticType destination_type =
                    resolve_type(ast::TypeReference{node.callee, node.location,
                                                    node.type_arguments},
                                 *active_type_parameters, &class_arities);
                if (destination_type.is_concrete() &&
                    (destination_type.concrete->kind() == TypeKind::String ||
                     destination_type.concrete->kind() == TypeKind::Unit))
                  throw CompileError{
                      node.location,
                      "type '" + destination_type.name() +
                          "' cannot be used as an explicit cast target"};
                if (node.arguments.size() != 1)
                  throw CompileError{node.location,
                                     "explicit cast to '" +
                                         destination_type.name() +
                                         "' expects exactly one argument"};
                const SemanticType source_type =
                    expression_type(*node.arguments.front());
                const auto enum_has_payload = [&](const SemanticType &type) {
                  if (!type.is_enum())
                    return false;
                  const ast::EnumDeclaration &declaration =
                      *enums.at(type.parameter);
                  return std::any_of(
                      declaration.cases.begin(), declaration.cases.end(),
                      [](const ast::EnumDeclaration::Case &enum_case) {
                        return !enum_case.payload_types.empty();
                      });
                };
                if (enum_has_payload(source_type) ||
                    enum_has_payload(destination_type))
                  throw CompileError{
                      node.location,
                      "enums with payloads cannot be explicitly cast"};
                if (!can_explicitly_cast(source_type, destination_type))
                  throw CompileError{node.location,
                                     "cannot explicitly cast type '" +
                                         source_type.name() + "' to '" +
                                         destination_type.name() + "'"};
                if ((source_type.is_pointer() ||
                     destination_type.is_pointer()) &&
                    !same_type(source_type, destination_type))
                  emit_warning(
                      DiagnosticCode::AnalyzerAmbiguousPointerCast,
                      node.location,
                      "explicit cast between '" + source_type.name() +
                          "' and '" + destination_type.name() +
                          "' has an ambiguous ownership contract",
                      {"keep the original owner alive and document whether "
                       "the converted pointer is borrowed or owning"});

                const bool source_numeric =
                    source_type.is_concrete() &&
                    (source_type.concrete->is_integer() ||
                     source_type.concrete->is_floating_point());
                const bool destination_numeric =
                    destination_type.is_concrete() &&
                    (destination_type.concrete->is_integer() ||
                     destination_type.concrete->is_floating_point());
                bool lossy_numeric_cast = false;
                if (source_numeric && destination_numeric &&
                    !std::holds_alternative<ast::IntegerLiteralExpression>(
                        node.arguments.front()->value)) {
                  const bool source_integer =
                      source_type.concrete->is_integer();
                  const bool destination_integer =
                      destination_type.concrete->is_integer();
                  if (source_integer && destination_integer)
                    lossy_numeric_cast =
                        destination_type.concrete->bit_width() <
                            source_type.concrete->bit_width() ||
                        destination_type.concrete->is_signed() !=
                            source_type.concrete->is_signed();
                  else if (!source_integer && destination_integer)
                    lossy_numeric_cast = true;
                  else if (!source_integer && !destination_integer)
                    lossy_numeric_cast =
                        destination_type.concrete->bit_width() <
                        source_type.concrete->bit_width();
                  else
                    lossy_numeric_cast = source_type.concrete->bit_width() >
                                         destination_type.concrete->bit_width();
                }
                if (lossy_numeric_cast)
                  emit_warning(
                      DiagnosticCode::AnalyzerLossyNumericCast, node.location,
                      "explicit cast from '" + source_type.name() + "' to '" +
                          destination_type.name() +
                          "' may lose range or precision",
                      {"use checkedCast[T] to reject altered values, "
                       "saturatingCast[T] to clamp them, or "
                       "truncatingCast[T] for intentional truncation"});
                return destination_type;
              }
              const auto callee_iterator =
                  find_in_context(functions, context_module, node.callee);
              if (callee_iterator == functions.end()) {
                if (ambiguous_functions.contains(node.callee))
                  throw CompileError{
                      node.location,
                      "function name '" + node.callee +
                          "' is ambiguous; use a qualified name"};
                throw CompileError{node.location,
                                   "unknown function '" + node.callee + "'"};
              }
              const ast::FunctionDeclaration &callee = *callee_iterator->second;
              if (callee.is_private && callee.module_name != context_module)
                throw CompileError{
                    node.location,
                    "function '" + global_key(callee.module_name, callee.name) +
                        "' is private"};
              if (!import_allows(context_module, callee.module_name,
                                 callee.name, node.callee))
                throw CompileError{node.location,
                                   "function '" + node.callee +
                                       "' is not imported in this module"};
              return declared_call_type(callee, node.type_arguments,
                                        node.arguments, node.location,
                                        node.callee, &expression);
            } else if constexpr (std::is_same_v<Node, ast::NewExpression>) {
              const auto iterator =
                  find_in_context(classes, context_module, node.class_name);
              if (iterator == classes.end())
                throw CompileError{node.location,
                                   "unknown class '" + node.class_name + "'"};
              const ast::ClassDeclaration &class_declaration =
                  *iterator->second;
              if (class_declaration.is_private &&
                  class_declaration.module_name != context_module)
                throw CompileError{node.location,
                                   "type '" + node.class_name + "' is private"};
              const std::size_t parameter_count =
                  class_declaration.constructor_parameters.size();
              const std::size_t field_count =
                  class_declaration.constructor_fields.size();
              if (node.arguments.size() != parameter_count + field_count)
                throw CompileError{
                    node.location,
                    "constructor '" + node.class_name + "' expects " +
                        std::to_string(parameter_count + field_count) +
                        " argument(s), got " +
                        std::to_string(node.arguments.size())};
              const std::unordered_set<std::string> class_parameters{
                  class_declaration.type_parameters.begin(),
                  class_declaration.type_parameters.end()};
              const bool infer_type_arguments =
                  node.type_arguments.empty() && !class_parameters.empty();
              SemanticType instance_type;
              if (infer_type_arguments) {
                std::vector<ast::TypeReference> symbolic_arguments;
                symbolic_arguments.reserve(
                    class_declaration.type_parameters.size());
                for (const std::string &parameter :
                     class_declaration.type_parameters)
                  symbolic_arguments.push_back(
                      ast::TypeReference{parameter, node.location, {}});
                const SemanticType instance_pattern = resolve_type(
                    ast::TypeReference{node.class_name, node.location,
                                       std::move(symbolic_arguments)},
                    class_parameters, &class_arities, context_module,
                    &scoped_type_aliases);
                std::unordered_map<std::string, SemanticType> inferred;
                const auto infer_from_type =
                    [&](const auto &self, const SemanticType &pattern,
                        const SemanticType &candidate) -> void {
                  const bool type_parameter =
                      !pattern.is_concrete() && !pattern.is_class() &&
                      !pattern.is_pointer() && !pattern.is_enum() &&
                      !pattern.is_function() &&
                      class_parameters.contains(pattern.parameter);
                  if (type_parameter) {
                    const auto [existing, inserted] =
                        inferred.emplace(pattern.parameter, candidate);
                    if (!inserted && !same_type(existing->second, candidate))
                      throw CompileError{
                          node.location,
                          "generic type parameter '" + pattern.parameter +
                              "' has incompatible argument types"};
                    return;
                  }
                  const bool same_outer_type =
                      pattern.concrete == candidate.concrete &&
                      pattern.parameter == candidate.parameter &&
                      pattern.class_type == candidate.class_type &&
                      pattern.pointer_type == candidate.pointer_type &&
                      pattern.enum_type == candidate.enum_type &&
                      pattern.function_type == candidate.function_type &&
                      pattern.type_arguments.size() ==
                          candidate.type_arguments.size();
                  if (!same_outer_type)
                    return;
                  for (std::size_t index = 0;
                       index < pattern.type_arguments.size(); ++index)
                    self(self, pattern.type_arguments[index],
                         candidate.type_arguments[index]);
                };
                for (std::size_t index = 0; index < node.arguments.size();
                     ++index) {
                  const ast::TypeReference &reference =
                      index < parameter_count
                          ? class_declaration.constructor_parameters[index].type
                          : *class_declaration
                                 .constructor_fields[index - parameter_count]
                                 .declared_type;
                  const SemanticType pattern =
                      resolve_type(reference, class_parameters, &class_arities);
                  const SemanticType candidate =
                      speculative_expression_type(*node.arguments[index]);
                  infer_from_type(infer_from_type, pattern, candidate);
                }
                if (contextual_expression == &expression &&
                    contextual_expected_type != nullptr)
                  infer_from_type(infer_from_type, instance_pattern,
                                  *contextual_expected_type);
                std::vector<SemanticType> ordered_arguments;
                ordered_arguments.reserve(
                    class_declaration.type_parameters.size());
                for (const std::string &parameter :
                     class_declaration.type_parameters) {
                  const auto argument = inferred.find(parameter);
                  if (argument == inferred.end())
                    throw CompileError{
                        node.location,
                        "generic type parameter '" + parameter +
                            "' is not constrained by constructor arguments or "
                            "context; help: add explicit type arguments"};
                  ordered_arguments.push_back(argument->second);
                }
                result.inferred_generic_arguments.insert_or_assign(
                    &expression, ordered_arguments);
                instance_type = substitute(instance_pattern, inferred);
              } else {
                instance_type = resolve_type(
                    ast::TypeReference{node.class_name, node.location,
                                       node.type_arguments},
                    *active_type_parameters, &class_arities, context_module,
                    &scoped_type_aliases);
              }
              if (active_type_substitutions != nullptr)
                instance_type = substitute(std::move(instance_type),
                                           *active_type_substitutions);
              const auto substitutions =
                  class_substitutions(class_declaration, instance_type);
              if (class_declaration.name == "DerivedHashing" &&
                  class_declaration.module_name ==
                      std::optional<std::string>{"std.hashing"}) {
                const SemanticType &candidate = substitutions.at("T");
                if (!supports_derivation(candidate,
                                         ast::DerivationKind::Hashing))
                  throw CompileError{
                      node.location,
                      "DerivedHashing requires a type that derives Hashing; '" +
                          candidate.name() + "' does not"};
              }
              for (const ast::TypeConstraint &constraint :
                   class_declaration.type_constraints) {
                const SemanticType &candidate =
                    substitutions.at(constraint.parameter);
                if (const auto kind =
                        derivation_constraint(constraint.trait.name);
                    kind.has_value() &&
                    constraint.trait.type_arguments.empty()) {
                  const bool satisfies =
                      *kind == ast::DerivationKind::Copy
                          ? satisfies_copy(candidate)
                          : supports_derivation(candidate, *kind);
                  if (!satisfies)
                    throw CompileError{node.location,
                                       "type '" + candidate.name() +
                                           "' does not satisfy constraint '" +
                                           constraint.trait.name +
                                           "' for type "
                                           "parameter '" +
                                           constraint.parameter + "'"};
                  continue;
                }
                TraitInstance requirement =
                    resolve_trait(constraint.trait, class_parameters);
                for (SemanticType &argument : requirement.type_arguments)
                  argument = substitute(std::move(argument), substitutions);
                if (!satisfies_active_trait(candidate, requirement))
                  throw CompileError{node.location,
                                     "type '" + candidate.name() +
                                         "' does not satisfy constraint '" +
                                         requirement.declaration->name +
                                         "' for type parameter '" +
                                         constraint.parameter + "'"};
              }
              SymbolTable initializer_symbols;
              for (std::size_t index = 0; index < parameter_count; ++index) {
                const auto &parameter =
                    class_declaration.constructor_parameters[index];
                SemanticType parameter_type = resolve_type(
                    parameter.type, class_parameters, &class_arities);
                parameter_type =
                    substitute(std::move(parameter_type), substitutions);
                validate_expression(
                    *node.arguments[index], parameter_type,
                    expression_location(*node.arguments[index]));
                initializer_symbols.emplace(
                    parameter.name, Symbol{parameter_type, false, true});
              }
              for (std::size_t index = 0; index < field_count; ++index) {
                const auto &field = class_declaration.constructor_fields[index];
                const SemanticType field_type = resolve_type(
                    *field.declared_type, class_parameters, &class_arities);
                const SemanticType concrete_field_type =
                    substitute(field_type, substitutions);
                const ast::Expression &argument =
                    *node.arguments[parameter_count + index];
                if (field.is_borrowed &&
                    std::holds_alternative<ast::MoveExpression>(argument.value))
                  throw CompileError{
                      expression_location(argument),
                      "borrowed field '" + field.name +
                          "' cannot be initialized with a moved value"};
                const bool previous_contextual_borrow_expression =
                    contextual_borrow_expression;
                contextual_borrow_expression = field.is_borrowed;
                validate_expression(argument, concrete_field_type,
                                    expression_location(argument));
                contextual_borrow_expression =
                    previous_contextual_borrow_expression;
                initializer_symbols.emplace(
                    field.name,
                    Symbol{concrete_field_type, field.is_mutable, true});
              }

              SymbolTable *previous_symbols = active_symbols;
              const auto *previous_type_parameters = active_type_parameters;
              const auto *previous_type_substitutions =
                  active_type_substitutions;
              const std::optional<std::string> previous_context_module =
                  context_module;
              active_symbols = &initializer_symbols;
              active_type_parameters = &class_parameters;
              active_type_substitutions = &substitutions;
              context_module = class_declaration.module_name;
              for (const ast::ValueDeclaration &field :
                   class_declaration.fields) {
                SemanticType field_type = resolve_type(
                    *field.declared_type, class_parameters, &class_arities,
                    context_module, &scoped_type_aliases);
                field_type = substitute(std::move(field_type), substitutions);
                if (field.initializer.has_value())
                  validate_expression(*field.initializer, field_type,
                                      field.location);
                initializer_symbols.emplace(
                    field.name, Symbol{field_type, field.is_mutable,
                                       field.initializer.has_value()});
              }
              active_symbols = previous_symbols;
              active_type_parameters = previous_type_parameters;
              active_type_substitutions = previous_type_substitutions;
              context_module = previous_context_module;
              return instance_type;
            } else if constexpr (std::is_same_v<Node,
                                                ast::MemberAccessExpression>) {
              const auto enum_name = qualified_expression_name(*node.object);
              const auto enum_iterator =
                  enum_name.has_value()
                      ? find_in_context(enums, context_module, *enum_name)
                      : enums.end();
              if (enum_name.has_value() && enum_iterator != enums.end() &&
                  (enum_name->find('.') == std::string::npos ||
                   !active_symbols->contains(
                       enum_name->substr(0, enum_name->find('.'))))) {
                const ast::EnumDeclaration &enum_declaration =
                    *enum_iterator->second;
                if (enum_declaration.is_private &&
                    enum_declaration.module_name != context_module)
                  throw CompileError{node.location,
                                     "type '" + *enum_name + "' is private"};
                if (!enum_declaration.type_parameters.empty())
                  throw CompileError{
                      node.location,
                      "generic enum cases require constructor syntax '" +
                          *enum_name + "." + node.member + "[Types](...)'"};
                const auto enum_case =
                    std::find_if(enum_declaration.cases.begin(),
                                 enum_declaration.cases.end(),
                                 [&](const ast::EnumDeclaration::Case &item) {
                                   return item.name == node.member;
                                 });
                if (enum_case == enum_declaration.cases.end())
                  throw CompileError{node.location, "enum '" + *enum_name +
                                                        "' has no case '" +
                                                        node.member + "'"};
                if (!enum_case->payload_types.empty())
                  throw CompileError{node.location,
                                     "enum case '" + *enum_name + "." +
                                         node.member +
                                         "' requires constructor arguments"};
                return SemanticType{nullptr, *enum_name, false,
                                    {},      false,      true};
              }
              if (const auto module = qualified_expression_name(*node.object);
                  module.has_value() && global_modules.contains(*module) &&
                  !active_symbols->contains(
                      module->substr(0, module->find('.')))) {
                const ResolvedGlobal *global = find_global(
                    std::optional<std::string>{*module}, node.member);
                if (global == nullptr)
                  throw CompileError{node.location,
                                     "module '" + *module +
                                         "' has no global value '" +
                                         node.member + "'"};
                if (global->declaration->declaration.is_private &&
                    global->declaration->module_name != context_module)
                  throw CompileError{node.location, "global value '" + *module +
                                                        "." + node.member +
                                                        "' is private"};
                const std::string spelling = *module + "." + node.member;
                if (!import_allows(
                        context_module, global->declaration->module_name,
                        global->declaration->declaration.name, spelling))
                  throw CompileError{node.location,
                                     "global value '" + spelling +
                                         "' is not imported in this module"};
                result.qualified_global_reads.insert_or_assign(
                    &node, global_key(global->declaration->module_name,
                                      global->declaration->declaration.name));
                return global->symbol.type;
              }
              const SemanticType object_type = expression_type(*node.object);
              if (!object_type.is_class())
                throw CompileError{node.location,
                                   "member access requires an object"};
              const ast::ClassDeclaration &class_declaration =
                  *classes.at(object_type.parameter);
              const auto substitutions =
                  class_substitutions(class_declaration, object_type);
              const std::unordered_set<std::string> class_parameters{
                  class_declaration.type_parameters.begin(),
                  class_declaration.type_parameters.end()};
              for (const auto &field : class_declaration.constructor_fields) {
                if (field.name == node.member) {
                  if (field.is_private &&
                      (owner == nullptr ||
                       owner->name != class_declaration.name))
                    throw CompileError{node.location,
                                       "field '" + node.member +
                                           "' is private in class '" +
                                           class_declaration.name + "'"};
                  if (field.is_internal &&
                      class_declaration.module_name != context_module)
                    throw CompileError{
                        node.location,
                        "field '" + node.member + "' is internal to module '" +
                            class_declaration.module_name.value_or("<entry>") +
                            "'"};
                  return substitute(resolve_type(*field.declared_type,
                                                 class_parameters,
                                                 &class_arities),
                                    substitutions);
                }
              }
              for (const auto &field : class_declaration.fields) {
                if (field.name == node.member) {
                  if (field.is_private &&
                      (owner == nullptr ||
                       owner->name != class_declaration.name))
                    throw CompileError{node.location,
                                       "field '" + node.member +
                                           "' is private in class '" +
                                           class_declaration.name + "'"};
                  if (field.is_internal &&
                      class_declaration.module_name != context_module)
                    throw CompileError{
                        node.location,
                        "field '" + node.member + "' is internal to module '" +
                            class_declaration.module_name.value_or("<entry>") +
                            "'"};
                  return substitute(resolve_type(*field.declared_type,
                                                 class_parameters,
                                                 &class_arities),
                                    substitutions);
                }
              }
              throw CompileError{node.location,
                                 "class '" + class_declaration.name +
                                     "' has no field '" + node.member + "'"};
            } else if constexpr (std::is_same_v<Node,
                                                ast::MethodCallExpression>) {
              if (const auto module = qualified_expression_name(*node.object);
                  module.has_value() &&
                  !active_symbols->contains(
                      module->substr(0, module->find('.')))) {
                const std::string qualified = *module + "." + node.method;
                if (const auto function =
                        find_in_context(functions, context_module, qualified);
                    function != functions.end()) {
                  if (function->second->is_private &&
                      function->second->module_name != context_module)
                    throw CompileError{node.location, "function '" + qualified +
                                                          "' is private"};
                  if (!import_allows(context_module,
                                     function->second->module_name,
                                     function->second->name, qualified))
                    throw CompileError{node.location,
                                       "function '" + qualified +
                                           "' is not imported in this module"};
                  return declared_call_type(
                      *function->second, node.type_arguments, node.arguments,
                      node.location, qualified, &expression);
                }
              }
              const auto enum_name = qualified_expression_name(*node.object);
              const auto enum_iterator =
                  enum_name.has_value()
                      ? find_in_context(enums, context_module, *enum_name)
                      : enums.end();
              if (enum_name.has_value() && enum_iterator != enums.end() &&
                  (enum_name->find('.') == std::string::npos ||
                   !active_symbols->contains(
                       enum_name->substr(0, enum_name->find('.'))))) {
                const ast::EnumDeclaration &enum_declaration =
                    *enum_iterator->second;
                if (enum_declaration.is_private &&
                    enum_declaration.module_name != context_module)
                  throw CompileError{node.location,
                                     "type '" + *enum_name + "' is private"};
                const auto enum_case =
                    std::find_if(enum_declaration.cases.begin(),
                                 enum_declaration.cases.end(),
                                 [&](const ast::EnumDeclaration::Case &item) {
                                   return item.name == node.method;
                                 });
                if (enum_case == enum_declaration.cases.end())
                  throw CompileError{node.location, "enum '" + *enum_name +
                                                        "' has no case '" +
                                                        node.method + "'"};
                if (node.arguments.size() != enum_case->payload_types.size())
                  throw CompileError{
                      node.location,
                      "enum case '" + *enum_name + "." + node.method +
                          "' expects " +
                          std::to_string(enum_case->payload_types.size()) +
                          " argument(s), got " +
                          std::to_string(node.arguments.size())};
                const std::unordered_set<std::string> enum_parameters{
                    enum_declaration.type_parameters.begin(),
                    enum_declaration.type_parameters.end()};
                const bool infer_type_arguments =
                    node.type_arguments.empty() && !enum_parameters.empty();
                SemanticType instance_type;
                std::unordered_map<std::string, SemanticType> substitutions;
                if (infer_type_arguments) {
                  std::vector<ast::TypeReference> symbolic_arguments;
                  symbolic_arguments.reserve(
                      enum_declaration.type_parameters.size());
                  for (const std::string &parameter :
                       enum_declaration.type_parameters)
                    symbolic_arguments.push_back(
                        ast::TypeReference{parameter, node.location, {}});
                  const SemanticType instance_pattern = resolve_type(
                      ast::TypeReference{*enum_name, node.location,
                                         std::move(symbolic_arguments)},
                      enum_parameters, &class_arities, context_module,
                      &scoped_type_aliases);
                  const auto infer_from_type =
                      [&](const auto &self, const SemanticType &pattern,
                          const SemanticType &candidate) -> void {
                    const bool type_parameter =
                        !pattern.is_concrete() && !pattern.is_class() &&
                        !pattern.is_pointer() && !pattern.is_enum() &&
                        !pattern.is_function() &&
                        enum_parameters.contains(pattern.parameter);
                    if (type_parameter) {
                      const auto [existing, inserted] =
                          substitutions.emplace(pattern.parameter, candidate);
                      if (!inserted && !same_type(existing->second, candidate))
                        throw CompileError{
                            node.location,
                            "generic type parameter '" + pattern.parameter +
                                "' has incompatible argument types"};
                      return;
                    }
                    const bool same_outer_type =
                        pattern.concrete == candidate.concrete &&
                        pattern.parameter == candidate.parameter &&
                        pattern.class_type == candidate.class_type &&
                        pattern.pointer_type == candidate.pointer_type &&
                        pattern.enum_type == candidate.enum_type &&
                        pattern.function_type == candidate.function_type &&
                        pattern.type_arguments.size() ==
                            candidate.type_arguments.size();
                    if (!same_outer_type)
                      return;
                    for (std::size_t index = 0;
                         index < pattern.type_arguments.size(); ++index)
                      self(self, pattern.type_arguments[index],
                           candidate.type_arguments[index]);
                  };
                  for (std::size_t index = 0; index < node.arguments.size();
                       ++index) {
                    const SemanticType pattern =
                        resolve_type(enum_case->payload_types[index],
                                     enum_parameters, &class_arities);
                    const SemanticType candidate =
                        speculative_expression_type(*node.arguments[index]);
                    infer_from_type(infer_from_type, pattern, candidate);
                  }
                  if (contextual_expression == &expression &&
                      contextual_expected_type != nullptr)
                    infer_from_type(infer_from_type, instance_pattern,
                                    *contextual_expected_type);
                  std::vector<SemanticType> ordered_arguments;
                  ordered_arguments.reserve(
                      enum_declaration.type_parameters.size());
                  for (const std::string &parameter :
                       enum_declaration.type_parameters) {
                    const auto argument = substitutions.find(parameter);
                    if (argument == substitutions.end())
                      throw CompileError{
                          node.location,
                          "generic type parameter '" + parameter +
                              "' is not constrained by enum payloads or "
                              "context; help: add explicit type arguments"};
                    ordered_arguments.push_back(argument->second);
                  }
                  result.inferred_generic_arguments.insert_or_assign(
                      &expression, ordered_arguments);
                  instance_type = substitute(instance_pattern, substitutions);
                } else {
                  instance_type =
                      resolve_type(ast::TypeReference{*enum_name, node.location,
                                                      node.type_arguments},
                                   *active_type_parameters, &class_arities,
                                   context_module, &scoped_type_aliases);
                  for (std::size_t index = 0;
                       index < enum_declaration.type_parameters.size(); ++index)
                    substitutions.emplace(
                        enum_declaration.type_parameters[index],
                        instance_type.type_arguments[index]);
                }
                if (active_type_substitutions != nullptr) {
                  instance_type = substitute(std::move(instance_type),
                                             *active_type_substitutions);
                  for (auto &[parameter, argument] : substitutions)
                    argument = substitute(std::move(argument),
                                          *active_type_substitutions);
                }
                for (std::size_t index = 0; index < node.arguments.size();
                     ++index) {
                  SemanticType expected =
                      resolve_type(enum_case->payload_types[index],
                                   enum_parameters, &class_arities);
                  validate_expression(
                      *node.arguments[index],
                      substitute(std::move(expected), substitutions),
                      expression_location(*node.arguments[index]));
                }
                return instance_type;
              }
              const SemanticType object_type = expression_type(*node.object);
              if (object_type.is_pointer()) {
                if (!node.type_arguments.empty())
                  throw CompileError{
                      node.location,
                      "Ptr methods do not accept type arguments"};
                if (node.method == "load") {
                  if (node.arguments.size() != 1)
                    throw CompileError{node.location,
                                       "Ptr.load expects one index"};
                  validate_expression(*node.arguments[0],
                                      SemanticType{&Type::usize_type()},
                                      expression_location(*node.arguments[0]));
                  return object_type.type_arguments.front();
                }
                if (node.method == "store" || node.method == "initialize" ||
                    node.method == "overwrite") {
                  if (const auto *identifier =
                          std::get_if<ast::IdentifierExpression>(
                              &node.object->value))
                    require_no_live_borrow(identifier->name, node.location,
                                           "mutated");
                  if (const auto *identifier =
                          std::get_if<ast::IdentifierExpression>(
                              &node.object->value);
                      identifier != nullptr &&
                      shared_borrow_values.contains(identifier->name))
                    throw CompileError{node.location,
                                       "cannot mutate through shared borrow '" +
                                           identifier->name + "'"};
                  if (node.arguments.size() != 2)
                    throw CompileError{node.location,
                                       "Ptr." + node.method +
                                           " expects an index and a value"};
                  validate_expression(*node.arguments[0],
                                      SemanticType{&Type::usize_type()},
                                      expression_location(*node.arguments[0]));
                  validate_expression(*node.arguments[1],
                                      object_type.type_arguments.front(),
                                      expression_location(*node.arguments[1]));
                  if (node.method == "store" &&
                      potentially_owns_value(
                          object_type.type_arguments.front()))
                    emit_warning(
                        DiagnosticCode::AnalyzerOwningPointerElementOverwritten,
                        node.location,
                        "Ptr.store may overwrite an initialized element of "
                        "owning type '" +
                            object_type.type_arguments.front().name() + "'",
                        {"load and destroy or move the previous element, or "
                         "use an initialization-specific abstraction"});
                  return SemanticType{&Type::unit_type()};
                }
                throw CompileError{node.location, "Ptr[T] has no method '" +
                                                      node.method + "'"};
              }
              std::unordered_map<std::string, SemanticType> substitutions;
              std::unordered_set<std::string> method_parameters;
              const ast::FunctionDeclaration *method = nullptr;
              const ast::ClassDeclaration *class_declaration = nullptr;
              const ast::TraitDeclaration *trait_declaration = nullptr;
              if (object_type.is_class()) {
                class_declaration = classes.at(object_type.parameter);
                substitutions =
                    class_substitutions(*class_declaration, object_type);
                method_parameters.insert(
                    class_declaration->type_parameters.begin(),
                    class_declaration->type_parameters.end());
                for (const ast::FunctionDeclaration &candidate :
                     class_declaration->methods) {
                  if (candidate.name == node.method)
                    method = &candidate;
                }
              } else {
                const auto constraint =
                    active_trait_constraints.find(object_type.parameter);
                if (constraint == active_trait_constraints.end())
                  throw CompileError{node.location,
                                     "method call requires an object or a "
                                     "trait-constrained value"};
                const TraitInstance *selected_constraint = nullptr;
                for (const TraitInstance &active : constraint->second) {
                  const auto candidate = std::find_if(
                      active.declaration->methods.begin(),
                      active.declaration->methods.end(),
                      [&](const ast::FunctionDeclaration &declaration) {
                        return declaration.name == node.method;
                      });
                  if (candidate == active.declaration->methods.end())
                    continue;
                  if (method != nullptr)
                    throw CompileError{
                        node.location,
                        "method '" + node.method +
                            "' is ambiguous between multiple trait "
                            "constraints"};
                  method = &*candidate;
                  selected_constraint = &active;
                }
                if (selected_constraint == nullptr)
                  throw CompileError{
                      node.location,
                      "no trait constraint for type parameter '" +
                          object_type.parameter + "' provides method '" +
                          node.method + "'"};
                trait_declaration = selected_constraint->declaration;
                method_parameters.insert(
                    trait_declaration->type_parameters.begin(),
                    trait_declaration->type_parameters.end());
                for (std::size_t index = 0;
                     index < trait_declaration->type_parameters.size(); ++index)
                  substitutions.emplace(
                      trait_declaration->type_parameters[index],
                      selected_constraint->type_arguments[index]);
              }
              if (method == nullptr)
                throw CompileError{node.location,
                                   std::string{class_declaration != nullptr
                                                   ? "class '"
                                                   : "trait '"} +
                                       (class_declaration != nullptr
                                            ? class_declaration->name
                                            : trait_declaration->name) +
                                       "' has no method '" + node.method + "'"};
              if (const auto *identifier =
                      std::get_if<ast::IdentifierExpression>(
                          &node.object->value);
                  identifier != nullptr &&
                  shared_borrow_values.contains(identifier->name) &&
                  !method->is_borrowing)
                throw CompileError{node.location,
                                   "shared borrow '" + identifier->name +
                                       "' can only call a borrow method"};
              if (!method->is_borrowing)
                if (const auto *identifier =
                        std::get_if<ast::IdentifierExpression>(
                            &node.object->value))
                  require_no_live_borrow(identifier->name, node.location,
                                         "mutated");
              if (class_declaration != nullptr && method->is_private &&
                  (owner == nullptr || owner->name != class_declaration->name))
                throw CompileError{node.location,
                                   "method '" + node.method +
                                       "' is private in class '" +
                                       class_declaration->name + "'"};
              if (class_declaration != nullptr && method->is_internal &&
                  class_declaration->module_name != context_module)
                throw CompileError{
                    node.location,
                    "method '" + node.method + "' is internal to module '" +
                        class_declaration->module_name.value_or("<entry>") +
                        "'"};
              const bool infer_method_type_arguments =
                  node.type_arguments.empty() &&
                  !method->type_parameters.empty();
              if (!infer_method_type_arguments &&
                  node.type_arguments.size() != method->type_parameters.size())
                throw CompileError{
                    node.location,
                    "method '" + node.method + "' expects " +
                        std::to_string(method->type_parameters.size()) +
                        " type argument(s), got " +
                        std::to_string(node.type_arguments.size())};
              if (node.arguments.size() != method->parameters.size())
                throw CompileError{
                    node.location,
                    "method '" + node.method + "' expects " +
                        std::to_string(method->parameters.size()) +
                        " argument(s), got " +
                        std::to_string(node.arguments.size())};
              if (class_declaration != nullptr &&
                  class_declaration->name == "Array" &&
                  class_declaration->module_name ==
                      std::optional<std::string>{"std.array"} &&
                  is_copy_only_array_method(node.method) &&
                  !satisfies_copy(substitutions.at("T")))
                throw CompileError{
                    node.location,
                    "Array." + node.method +
                        " requires a Copy element type; use remove, pop, or "
                        "replace to transfer an owned element"};
              if (class_declaration != nullptr &&
                  class_declaration->name == "Array" &&
                  class_declaration->module_name ==
                      std::optional<std::string>{"std.array"} &&
                  (node.method == "push" || node.method == "set" ||
                   node.method == "replace") &&
                  potentially_owns_value(substitutions.at("T")) &&
                  !node.arguments.empty() &&
                  std::holds_alternative<ast::IdentifierExpression>(
                      node.arguments.back()->value))
                throw CompileError{
                    expression_location(*node.arguments.back()),
                    "transferring an owned Array element requires an "
                    "explicit move"};
              if (class_declaration != nullptr &&
                  class_declaration->name == "HashSet" &&
                  class_declaration->module_name ==
                      std::optional<std::string>{"std.hashset"} &&
                  node.method == "iterator" &&
                  !satisfies_copy(substitutions.at("T")))
                throw CompileError{
                    node.location,
                    "HashSet.iterator requires a Copy element type"};
              if (class_declaration != nullptr &&
                  class_declaration->name == "HashMap" &&
                  class_declaration->module_name ==
                      std::optional<std::string>{"std.hashmap"}) {
                const bool key_copy = satisfies_copy(substitutions.at("K"));
                const bool value_copy = satisfies_copy(substitutions.at("V"));
                if ((node.method == "entries" && (!key_copy || !value_copy)) ||
                    (node.method == "keys" && !key_copy) ||
                    ((node.method == "values" || node.method == "getOption") &&
                     !value_copy))
                  throw CompileError{
                      node.location,
                      "HashMap." + node.method +
                          " requires Copy for every returned element type"};
              }
              method_parameters.insert(method->type_parameters.begin(),
                                       method->type_parameters.end());
              for (std::size_t index = 0; index < node.type_arguments.size();
                   ++index) {
                SemanticType argument =
                    resolve_type(node.type_arguments[index],
                                 *active_type_parameters, &class_arities);
                if (active_type_substitutions != nullptr)
                  argument = substitute(std::move(argument),
                                        *active_type_substitutions);
                substitutions.emplace(method->type_parameters[index],
                                      std::move(argument));
              }
              if (infer_method_type_arguments) {
                const auto infer_from_type =
                    [&](const auto &self, const SemanticType &pattern,
                        const SemanticType &candidate) -> void {
                  const bool type_parameter =
                      !pattern.is_concrete() && !pattern.is_class() &&
                      !pattern.is_pointer() && !pattern.is_enum() &&
                      !pattern.is_function() &&
                      std::find(method->type_parameters.begin(),
                                method->type_parameters.end(),
                                pattern.parameter) !=
                          method->type_parameters.end();
                  if (type_parameter) {
                    const auto [existing, inserted] =
                        substitutions.emplace(pattern.parameter, candidate);
                    if (!inserted && !same_type(existing->second, candidate))
                      throw CompileError{
                          node.location,
                          "generic type parameter '" + pattern.parameter +
                              "' has incompatible argument types"};
                    return;
                  }
                  const bool same_outer_type =
                      pattern.concrete == candidate.concrete &&
                      pattern.parameter == candidate.parameter &&
                      pattern.class_type == candidate.class_type &&
                      pattern.pointer_type == candidate.pointer_type &&
                      pattern.enum_type == candidate.enum_type &&
                      pattern.function_type == candidate.function_type &&
                      pattern.type_arguments.size() ==
                          candidate.type_arguments.size();
                  if (!same_outer_type)
                    return;
                  for (std::size_t index = 0;
                       index < pattern.type_arguments.size(); ++index)
                    self(self, pattern.type_arguments[index],
                         candidate.type_arguments[index]);
                };
                for (std::size_t index = 0; index < node.arguments.size();
                     ++index) {
                  SemanticType pattern =
                      resolve_type(method->parameters[index].type,
                                   method_parameters, &class_arities);
                  pattern = substitute(std::move(pattern), substitutions);
                  const SemanticType candidate =
                      speculative_expression_type(*node.arguments[index]);
                  infer_from_type(infer_from_type, pattern, candidate);
                }
                if (contextual_expression == &expression &&
                    contextual_expected_type != nullptr) {
                  SemanticType return_pattern = resolve_type(
                      method->return_type, method_parameters, &class_arities);
                  return_pattern =
                      substitute(std::move(return_pattern), substitutions);
                  infer_from_type(infer_from_type, return_pattern,
                                  *contextual_expected_type);
                }
                std::vector<SemanticType> inferred;
                inferred.reserve(method->type_parameters.size());
                for (const std::string &parameter : method->type_parameters) {
                  if (!substitutions.contains(parameter))
                    throw CompileError{
                        node.location,
                        "generic type parameter '" + parameter +
                            "' is not constrained by call arguments; help: "
                            "add explicit type arguments"};
                  inferred.push_back(substitutions.at(parameter));
                }
                result.inferred_generic_arguments.insert_or_assign(
                    &expression, std::move(inferred));
              }
              for (const ast::TypeConstraint &constraint :
                   method->type_constraints) {
                const SemanticType &candidate =
                    substitutions.at(constraint.parameter);
                if (const auto kind =
                        derivation_constraint(constraint.trait.name);
                    kind.has_value() &&
                    constraint.trait.type_arguments.empty()) {
                  const bool satisfies =
                      *kind == ast::DerivationKind::Copy
                          ? satisfies_copy(candidate)
                          : supports_derivation(candidate, *kind);
                  if (!satisfies)
                    throw CompileError{node.location,
                                       "type '" + candidate.name() +
                                           "' does not satisfy constraint '" +
                                           constraint.trait.name +
                                           "' for type "
                                           "parameter '" +
                                           constraint.parameter + "'"};
                  continue;
                }
                TraitInstance requirement =
                    resolve_trait(constraint.trait, method_parameters);
                for (SemanticType &argument : requirement.type_arguments)
                  argument = substitute(std::move(argument), substitutions);
                if (!satisfies_active_trait(candidate, requirement))
                  throw CompileError{node.location,
                                     "type '" + candidate.name() +
                                         "' does not satisfy constraint '" +
                                         requirement.declaration->name +
                                         "' for type parameter '" +
                                         constraint.parameter + "'"};
              }
              for (std::size_t index = 0; index < node.arguments.size();
                   ++index) {
                const SemanticType expected =
                    resolve_type(method->parameters[index].type,
                                 method_parameters, &class_arities);
                const SemanticType substituted_expected =
                    substitute(expected, substitutions);
                const bool transfers_collection_value =
                    class_declaration != nullptr &&
                    ((class_declaration->name == "HashSet" &&
                      class_declaration->module_name ==
                          std::optional<std::string>{"std.hashset"} &&
                      node.method == "add") ||
                     (class_declaration->name == "HashMap" &&
                      class_declaration->module_name ==
                          std::optional<std::string>{"std.hashmap"} &&
                      node.method == "put") ||
                     (class_declaration->name == "ArrayBuilder" &&
                      class_declaration->module_name ==
                          std::optional<std::string>{"std.array_builder"} &&
                      node.method == "add") ||
                     (class_declaration->name == "SetBuilder" &&
                      class_declaration->module_name ==
                          std::optional<std::string>{"std.hashset"} &&
                      node.method == "add") ||
                     (class_declaration->name == "MapBuilder" &&
                      class_declaration->module_name ==
                          std::optional<std::string>{"std.hashmap"} &&
                      node.method == "add"));
                if (transfers_collection_value &&
                    potentially_owns_value(substituted_expected) &&
                    std::holds_alternative<ast::IdentifierExpression>(
                        node.arguments[index]->value))
                  throw CompileError{
                      expression_location(*node.arguments[index]),
                      "transferring an owned collection element requires an "
                      "explicit move"};
                const bool observes_hash_value =
                    (trait_declaration != nullptr &&
                     trait_declaration->name == "Hashing") ||
                    (class_declaration != nullptr &&
                     class_declaration->name == "HashSet" &&
                     class_declaration->module_name ==
                         std::optional<std::string>{"std.hashset"} &&
                     (node.method == "contains" || node.method == "remove")) ||
                    (class_declaration != nullptr &&
                     class_declaration->name == "HashMap" &&
                     class_declaration->module_name ==
                         std::optional<std::string>{"std.hashmap"} &&
                     index == 0 &&
                     (node.method == "containsKey" ||
                      node.method == "getOption" || node.method == "remove"));
                if (observes_hash_value) {
                  const SemanticType actual =
                      expression_type(*node.arguments[index]);
                  if (!same_type(actual, substituted_expected))
                    throw CompileError{
                        expression_location(*node.arguments[index]),
                        "cannot use expression of type '" + actual.name() +
                            "' where type '" + substituted_expected.name() +
                            "' is required"};
                  continue;
                }
                const bool observes_owned_callback =
                    class_declaration != nullptr &&
                    (((class_declaration->name == "Iterator" && index == 0) &&
                      class_declaration->module_name ==
                          std::optional<std::string>{"std.iterator"} &&
                      node.method == "filter" &&
                      potentially_owns_value(substitutions.at("T"))) ||
                     (class_declaration->name == "Array" &&
                      class_declaration->module_name ==
                          std::optional<std::string>{"std.array"} &&
                      ((node.method == "withValue" && index == 1) ||
                       (node.method == "foreach" && index == 0)) &&
                      potentially_owns_value(substitutions.at("T"))));
                if (observes_owned_callback &&
                    !std::holds_alternative<ast::LambdaExpression>(
                        node.arguments[index]->value))
                  throw CompileError{
                      expression_location(*node.arguments[index]),
                      "observing an owned collection element requires a "
                      "bounded lambda literal"};
                const bool previous_contextual_borrow =
                    contextual_borrow_lambda_parameters;
                const bool previous_contextual_borrow_expression =
                    contextual_borrow_expression;
                contextual_borrow_lambda_parameters = observes_owned_callback;
                contextual_borrow_expression =
                    method->parameters[index].ownership ==
                    ast::ParameterOwnership::Borrow;
                validate_expression(
                    *node.arguments[index], substituted_expected,
                    expression_location(*node.arguments[index]));
                contextual_borrow_lambda_parameters =
                    previous_contextual_borrow;
                contextual_borrow_expression =
                    previous_contextual_borrow_expression;
              }
              if (method->is_consuming) {
                require_guard_transfer_allowed(*node.object, node.location);
                if (const auto *identifier =
                        std::get_if<ast::IdentifierExpression>(
                            &node.object->value)) {
                  if (borrowed_values.contains(identifier->name))
                    throw CompileError{node.location,
                                       "borrowed value '" + identifier->name +
                                           "' cannot call a consuming method"};
                  if (deferred_values.contains(identifier->name))
                    throw CompileError{
                        node.location,
                        "owning value '" + identifier->name +
                            "' is scheduled for deferred cleanup"};
                  if (owner_field_names.contains(identifier->name))
                    throw CompileError{node.location,
                                       "consuming field '" + identifier->name +
                                           "' requires an explicit move"};
                  if (transfer_protected_values.contains(identifier->name))
                    throw CompileError{
                        node.location,
                        "owning value '" + identifier->name +
                            "' cannot be consumed from a loop, branch "
                            "expression, or closure"};
                  if (!active_symbols->contains(identifier->name) &&
                      visible_global(identifier->name) != nullptr)
                    throw CompileError{node.location,
                                       "owning global value '" +
                                           identifier->name +
                                           "' cannot be consumed"};
                  active_symbols->at(identifier->name).is_initialized = false;
                  active_symbols->at(identifier->name).may_be_initialized =
                      false;
                } else if (!std::holds_alternative<ast::MoveExpression>(
                               node.object->value) &&
                           !std::holds_alternative<ast::NewExpression>(
                               node.object->value) &&
                           !std::holds_alternative<ast::CallExpression>(
                               node.object->value) &&
                           !std::holds_alternative<ast::MethodCallExpression>(
                               node.object->value)) {
                  throw CompileError{
                      node.location,
                      "consuming method requires an owning local, explicit "
                      "move, or temporary object"};
                }
              } else if (potentially_owns_value(object_type) &&
                         (std::holds_alternative<ast::NewExpression>(
                              node.object->value) ||
                          std::holds_alternative<ast::CallExpression>(
                              node.object->value) ||
                          std::holds_alternative<ast::MethodCallExpression>(
                              node.object->value))) {
                emit_warning(
                    DiagnosticCode::AnalyzerBorrowedTemporaryOwner,
                    node.location,
                    "non-consuming method '" + node.method +
                        "' borrows a temporary owner of type '" +
                        object_type.name() + "' that is then abandoned",
                    {"bind the temporary to a local and schedule cleanup, or "
                     "call a consume method"});
              }
              return substitute(resolve_type(method->return_type,
                                             method_parameters, &class_arities),
                                substitutions);
            } else if constexpr (std::is_same_v<Node, ast::IfExpression>) {
              validate_expression(*node.condition,
                                  SemanticType{&Type::bool_type()},
                                  expression_location(*node.condition));
              const auto previous_transfer_protected =
                  transfer_protected_values;
              for (const auto &[name, symbol] : *active_symbols) {
                static_cast<void>(symbol);
                transfer_protected_values.insert(name);
              }
              const SemanticType then_type =
                  expression_type(*node.then_expression);
              const SemanticType else_type =
                  expression_type(*node.else_expression);
              transfer_protected_values = previous_transfer_protected;
              if (!same_type(then_type, else_type))
                throw CompileError{
                    node.location,
                    "if expression branches must have the same type, got '" +
                        then_type.name() + "' and '" + else_type.name() + "'"};
              if (then_type.is_concrete() &&
                  then_type.concrete->kind() == TypeKind::Unit)
                throw CompileError{
                    node.location,
                    "if expressions cannot produce a Unit value"};
              return then_type;
            } else if constexpr (std::is_same_v<Node, ast::MatchExpression>) {
              const SemanticType scrutinee_type =
                  expression_type(*node.scrutinee);
              const auto *scrutinee_identifier =
                  std::get_if<ast::IdentifierExpression>(
                      &node.scrutinee->value);
              const bool borrows_scrutinee =
                  scrutinee_identifier != nullptr &&
                  borrowed_values.contains(scrutinee_identifier->name);
              if (scrutinee_type.is_enum() &&
                  aggregate_owns_value(scrutinee_type) &&
                  std::holds_alternative<ast::IdentifierExpression>(
                      node.scrutinee->value) &&
                  !borrows_scrutinee)
                throw CompileError{
                    node.location,
                    "matching an owning enum requires an explicit move"};
              const auto previous_transfer_protected =
                  transfer_protected_values;
              for (const auto &[name, symbol] : *active_symbols) {
                static_cast<void>(symbol);
                transfer_protected_values.insert(name);
              }
              if (node.arms.empty())
                throw CompileError{node.location,
                                   "match requires at least one case"};

              const ast::EnumDeclaration *enum_declaration =
                  scrutinee_type.is_enum() ? enums.at(scrutinee_type.parameter)
                                           : nullptr;
              std::unordered_map<std::string, SemanticType> substitutions;
              for (std::size_t index = 0;
                   enum_declaration != nullptr &&
                   index < enum_declaration->type_parameters.size();
                   ++index) {
                substitutions.emplace(enum_declaration->type_parameters[index],
                                      scrutinee_type.type_arguments[index]);
              }
              const std::unordered_set<std::string> enum_parameters =
                  enum_declaration == nullptr
                      ? std::unordered_set<std::string>{}
                      : std::unordered_set<std::string>{
                            enum_declaration->type_parameters.begin(),
                            enum_declaration->type_parameters.end()};
              std::unordered_set<std::string> matched_cases;
              std::unordered_set<std::string> matched_literals;
              std::unordered_map<std::string, std::string>
                  matched_literal_spellings;
              bool wildcard_handled = false;
              bool true_handled = false;
              bool false_handled = false;
              std::optional<SemanticType> result_type;
              for (const ast::MatchExpression::Arm &arm : node.arms) {
                if (wildcard_handled)
                  throw CompileError{
                      arm.location,
                      "match arm is unreachable after wildcard pattern"};
                const ast::EnumDeclaration::Case *enum_case = nullptr;
                if (enum_declaration != nullptr && !arm.is_wildcard &&
                    !arm.case_name.empty()) {
                  const auto found = std::find_if(
                      enum_declaration->cases.begin(),
                      enum_declaration->cases.end(),
                      [&](const ast::EnumDeclaration::Case &candidate) {
                        return candidate.name == arm.case_name;
                      });
                  if (found == enum_declaration->cases.end())
                    throw CompileError{arm.location,
                                       "enum '" + enum_declaration->name +
                                           "' has no case '" + arm.case_name +
                                           "'"};
                  enum_case = &*found;
                  if (matched_cases.contains(arm.case_name))
                    throw CompileError{arm.location,
                                       "match case '" + arm.case_name +
                                           "' is already handled"};
                  if (!arm.guard)
                    matched_cases.insert(arm.case_name);
                } else if (enum_declaration != nullptr && !arm.is_wildcard) {
                  throw CompileError{arm.location,
                                     "enum matches require enum case patterns"};
                } else if (enum_declaration == nullptr && !arm.is_wildcard &&
                           arm.literal == nullptr) {
                  throw CompileError{
                      arm.location,
                      "literal match requires a literal or '_' pattern"};
                }
                if (enum_case != nullptr &&
                    arm.bindings.size() != enum_case->payload_types.size())
                  throw CompileError{
                      arm.location,
                      "enum case '" + arm.case_name + "' contains " +
                          std::to_string(enum_case->payload_types.size()) +
                          " value(s), but the pattern binds " +
                          std::to_string(arm.bindings.size())};

                if (arm.literal && enum_case == nullptr) {
                  const auto is_literal_pattern =
                      [&](const ast::Expression &expression,
                          const auto &self) -> bool {
                    return std::visit(
                        [&](const auto &literal_node) -> bool {
                          using Literal = std::decay_t<decltype(literal_node)>;
                          if constexpr (
                              std::is_same_v<Literal,
                                             ast::IntegerLiteralExpression> ||
                              std::is_same_v<Literal,
                                             ast::DoubleLiteralExpression> ||
                              std::is_same_v<Literal,
                                             ast::BooleanLiteralExpression> ||
                              std::is_same_v<Literal,
                                             ast::StringLiteralExpression> ||
                              std::is_same_v<Literal,
                                             ast::CharacterLiteralExpression>)
                            return true;
                          else if constexpr (std::is_same_v<
                                                 Literal,
                                                 ast::CallExpression>) {
                            static const std::unordered_set<std::string>
                                literal_conversions{
                                    "byte",   "ubyte", "short", "ushort",
                                    "int",    "uint",  "long",  "ulong",
                                    "isize",  "usize", "char",  "bool",
                                    "string", "float", "double"};
                            if (!literal_conversions.contains(
                                    literal_node.callee) ||
                                literal_node.arguments.size() != 1)
                              return false;
                            return self(*literal_node.arguments.front(), self);
                          } else if constexpr (std::is_same_v<
                                                   Literal,
                                                   ast::UnaryExpression>) {
                            return literal_node.operation ==
                                       ast::UnaryOperator::Negate &&
                                   self(*literal_node.operand, self);
                          } else
                            return false;
                        },
                        expression.value);
                  };
                  if (!is_literal_pattern(*arm.literal, is_literal_pattern))
                    throw CompileError{arm.location,
                                       "match pattern must be a literal"};
                  const SemanticType literal_type =
                      expression_type(*arm.literal);
                  if (!same_type(literal_type, scrutinee_type))
                    throw CompileError{arm.location,
                                       "literal pattern type '" +
                                           literal_type.name() +
                                           "' does not match scrutinee type '" +
                                           scrutinee_type.name() + "'"};
                  const constant::Value literal_value = constant::evaluate(
                      *arm.literal, scrutinee_type.concrete,
                      [](const std::optional<std::string> &, std::string_view,
                         SourceLocation) -> std::optional<constant::Value> {
                        return std::nullopt;
                      });
                  const std::string key =
                      constant::canonical_match_key(literal_value);
                  std::string spelling = key;
                  if (const auto *boolean =
                          std::get_if<bool>(&literal_value.data))
                    spelling = *boolean ? "true" : "false";
                  else if (const auto *integer =
                               std::get_if<std::uint64_t>(&literal_value.data))
                    spelling = std::to_string(*integer);
                  const auto previous_spelling =
                      matched_literal_spellings.find(key);
                  if (arm.guard && matched_literals.contains(key))
                    throw CompileError{arm.location,
                                       "literal pattern '" +
                                           previous_spelling->second +
                                           "' is already handled"};
                  if (!arm.guard && !matched_literals.insert(key).second)
                    throw CompileError{arm.location,
                                       "literal pattern '" +
                                           previous_spelling->second +
                                           "' is already handled"};
                  if (!arm.guard) {
                    matched_literal_spellings.emplace(key, spelling);
                    if (const auto *boolean =
                            std::get_if<bool>(&literal_value.data))
                      (*boolean ? true_handled : false_handled) = true;
                  }
                }
                if (arm.is_wildcard && !arm.guard)
                  wildcard_handled = true;

                SymbolTable arm_symbols = *active_symbols;
                std::unordered_set<std::string> binding_names;
                for (std::size_t index = 0; index < arm.bindings.size();
                     ++index) {
                  if (!binding_names.insert(arm.bindings[index]).second)
                    throw CompileError{arm.location,
                                       "pattern binding '" +
                                           arm.bindings[index] +
                                           "' is already declared"};
                  SemanticType payload_type =
                      resolve_type(enum_case->payload_types[index],
                                   enum_parameters, &class_arities);
                  payload_type =
                      substitute(std::move(payload_type), substitutions);
                  arm_symbols.insert_or_assign(
                      arm.bindings[index],
                      Symbol{std::move(payload_type), false, true});
                }
                SymbolTable *previous_symbols = active_symbols;
                active_symbols = &arm_symbols;
                const auto arm_transfer_protected = transfer_protected_values;
                const auto arm_borrowed_values = borrowed_values;
                for (const std::string &binding : arm.bindings)
                  transfer_protected_values.erase(binding);
                if (borrows_scrutinee)
                  for (const std::string &binding : arm.bindings)
                    borrowed_values.insert(binding);
                if (arm.guard) {
                  for (const std::string &binding : arm.bindings) {
                    transfer_protected_values.insert(binding);
                    match_guard_protected_values.insert(binding);
                  }
                  const SemanticType guard_type = expression_type(*arm.guard);
                  for (const std::string &binding : arm.bindings) {
                    match_guard_protected_values.erase(binding);
                    transfer_protected_values.erase(binding);
                  }
                  if (!guard_type.is_concrete() ||
                      guard_type.concrete->kind() != TypeKind::Bool)
                    throw CompileError{
                        arm.location,
                        "match guard must have type 'bool', got '" +
                            guard_type.name() + "'"};
                }
                const SemanticType arm_type = expression_type(*arm.expression);
                transfer_protected_values = arm_transfer_protected;
                borrowed_values = arm_borrowed_values;
                active_symbols = previous_symbols;
                if (arm_type.is_concrete() &&
                    arm_type.concrete->kind() == TypeKind::Unit)
                  throw CompileError{
                      arm.location,
                      "match expressions cannot produce a Unit value"};
                if (!result_type.has_value()) {
                  result_type = arm_type;
                } else if (!same_type(*result_type, arm_type)) {
                  throw CompileError{
                      arm.location,
                      "match cases must have the same type, got '" +
                          result_type->name() + "' and '" + arm_type.name() +
                          "'"};
                }
              }
              std::string missing_cases;
              if (enum_declaration != nullptr && !wildcard_handled)
                for (const ast::EnumDeclaration::Case &enum_case :
                     enum_declaration->cases) {
                  if (matched_cases.contains(enum_case.name))
                    continue;
                  if (!missing_cases.empty())
                    missing_cases += ", ";
                  missing_cases += enum_case.name;
                }
              if (!missing_cases.empty())
                throw CompileError{node.location,
                                   "non-exhaustive match for enum '" +
                                       enum_declaration->name +
                                       "': missing case(s): " + missing_cases};
              if (enum_declaration == nullptr && !wildcard_handled &&
                  !(scrutinee_type.is_concrete() &&
                    scrutinee_type.concrete->kind() == TypeKind::Bool &&
                    true_handled && false_handled))
                throw CompileError{node.location,
                                   "non-exhaustive match for type '" +
                                       scrutinee_type.name() + "'"};
              transfer_protected_values = previous_transfer_protected;
              return *result_type;
            } else if constexpr (std::is_same_v<Node, ast::MoveExpression>) {
              const auto *identifier =
                  std::get_if<ast::IdentifierExpression>(&node.operand->value);
              if (identifier == nullptr)
                throw CompileError{node.location,
                                   "move requires a local value identifier"};
              if (borrowed_values.contains(identifier->name))
                throw CompileError{node.location, "borrowed value '" +
                                                      identifier->name +
                                                      "' cannot be moved"};
              require_no_live_borrow(identifier->name, node.location);
              require_guard_transfer_allowed(*node.operand, node.location);
              if (transfer_protected_values.contains(identifier->name))
                throw CompileError{
                    node.location,
                    "owning value '" + identifier->name +
                        "' cannot be moved from a loop, branch expression, "
                        "or closure"};
              if (deferred_values.contains(identifier->name))
                throw CompileError{node.location,
                                   "owning value '" + identifier->name +
                                       "' is scheduled for deferred cleanup"};
              if (!active_symbols->contains(identifier->name) &&
                  visible_global(identifier->name) != nullptr)
                throw CompileError{node.location, "owning global value '" +
                                                      identifier->name +
                                                      "' cannot be moved"};
              const SemanticType moved_type = expression_type(*node.operand);
              const bool has_generic_argument = std::any_of(
                  moved_type.type_arguments.begin(),
                  moved_type.type_arguments.end(),
                  [&](const SemanticType &argument) {
                    return !argument.is_concrete() && !argument.is_class() &&
                           !argument.is_enum() && !argument.is_pointer() &&
                           !argument.is_function();
                  });
              const bool is_value_struct =
                  moved_type.is_class() &&
                  classes.at(moved_type.parameter)->is_value_type;
              if (is_value_struct && !has_generic_argument &&
                  !potentially_owns_value(moved_type))
                throw CompileError{
                    node.location,
                    "non-owning struct values are copied and cannot be moved"};
              if (moved_type.is_enum() && !has_generic_argument &&
                  !potentially_owns_value(moved_type))
                throw CompileError{
                    node.location,
                    "non-owning enum values are copied and cannot be moved"};
              if (moved_type.is_concrete())
                throw CompileError{
                    node.location,
                    "move requires an owning class, function, pointer, or "
                    "aggregate value"};
              active_symbols->at(identifier->name).is_initialized = false;
              active_symbols->at(identifier->name).may_be_initialized = false;
              return moved_type;
            } else if constexpr (std::is_same_v<Node, ast::TryExpression>) {
              if (inside_lambda)
                throw CompileError{
                    node.location,
                    "operator '?' is not supported inside lambda literals"};
              if (inside_defer)
                throw CompileError{
                    node.location,
                    "operator '?' is not supported in deferred actions"};
              const SemanticType operand_type = expression_type(*node.operand);
              if (!operand_type.is_enum() ||
                  (operand_type.parameter != "Option" &&
                   operand_type.parameter != "Result"))
                throw CompileError{
                    node.location,
                    "operator '?' requires an Option[T] or Result[T, E]"};
              if (!return_type.is_enum() ||
                  return_type.parameter != operand_type.parameter)
                throw CompileError{
                    node.location,
                    "operator '?' requires the enclosing function to return " +
                        operand_type.parameter};
              if (operand_type.parameter == "Result" &&
                  !same_type(operand_type.type_arguments[1],
                             return_type.type_arguments[1]))
                throw CompileError{
                    node.location,
                    "operator '?' cannot propagate error type '" +
                        operand_type.type_arguments[1].name() +
                        "' from a function returning error type '" +
                        return_type.type_arguments[1].name() + "'"};
              if (aggregate_owns_value(operand_type) &&
                  !std::holds_alternative<ast::MoveExpression>(
                      node.operand->value))
                throw CompileError{node.location,
                                   "propagating owning aggregate '" +
                                       operand_type.name() +
                                       "' requires an explicit move"};
              for (const auto &[name, declaration] : local_declarations) {
                const auto symbol = active_symbols->find(name);
                if (symbol == active_symbols->end() ||
                    !symbol->second.may_be_initialized ||
                    deferred_values.contains(name) ||
                    !potentially_owns_value(symbol->second.type))
                  continue;
                const std::string cleanup = symbol->second.type.is_pointer()
                                                ? "defer free(" + name + ")"
                                                : "defer delete " + name;
                emit_warning(
                    DiagnosticCode::AnalyzerUnprotectedEarlyExit, node.location,
                    "operator '?' may exit while owning value '" + name +
                        "' is not protected by deferred cleanup",
                    {"schedule cleanup before this expression with '" +
                     cleanup + "'"},
                    {DiagnosticLocation{declaration,
                                        "owning value declared here"}});
              }
              return operand_type.type_arguments.front();
            } else if constexpr (std::is_same_v<Node, ast::UnaryExpression>) {
              const SemanticType operand_type = expression_type(*node.operand);
              if (node.operation == ast::UnaryOperator::LogicalNot) {
                if (!operand_type.is_concrete() ||
                    operand_type.concrete->kind() != TypeKind::Bool) {
                  throw CompileError{
                      node.location,
                      "operator '!' requires an operand of type 'bool'"};
                }
                return SemanticType{&Type::bool_type(), {}};
              }

              if (!operand_type.is_concrete() ||
                  (!operand_type.concrete->is_floating_point() &&
                   (!operand_type.concrete->is_integer() ||
                    !operand_type.concrete->is_signed()))) {
                throw CompileError{
                    node.location,
                    "unary operator '-' requires a signed integer or "
                    "floating-point operand"};
              }
              return operand_type;
            } else {
              static_assert(std::is_same_v<Node, ast::BinaryExpression>);
              const SemanticType left_type = expression_type(*node.left);
              const bool is_shift =
                  node.operation == ast::BinaryOperator::ShiftLeft ||
                  node.operation == ast::BinaryOperator::ShiftRight;
              SemanticType right_type;
              if (is_shift &&
                  std::holds_alternative<ast::IntegerLiteralExpression>(
                      node.right->value)) {
                if (!integer_literal_fits(*node.right, Type::usize_type()))
                  throw CompileError{
                      node.location,
                      "integer literal is outside the unsigned usize range"};
                right_type = SemanticType{&Type::usize_type(), {}};
              } else if (is_shift &&
                         std::holds_alternative<ast::UnaryExpression>(
                             node.right->value)) {
                throw CompileError{
                    node.location,
                    "integer literal is outside the unsigned usize range"};
              } else {
                right_type = expression_type(*node.right);
              }
              if (is_shift) {
                if (!left_type.is_concrete() ||
                    !left_type.concrete->is_integer())
                  throw CompileError{
                      node.location,
                      "shift operators require an integer left operand"};
                if (!right_type.is_concrete() ||
                    right_type.concrete->kind() != TypeKind::USize)
                  throw CompileError{node.location,
                                     "shift count must have type usize"};
                return left_type;
              }
              if (!same_type(left_type, right_type)) {
                const bool bitwise =
                    node.operation == ast::BinaryOperator::BitwiseAnd ||
                    node.operation == ast::BinaryOperator::BitwiseXor ||
                    node.operation == ast::BinaryOperator::BitwiseOr;
                throw CompileError{
                    node.location,
                    std::string{bitwise ? "bitwise operands must have the same "
                                          "integer type, got '"
                                        : "binary operator operands must have "
                                          "the same type, got '"} +
                        left_type.name() + "' and '" + right_type.name() + "'"};
              }

              const bool is_concrete = left_type.is_concrete();
              const TypeKind kind =
                  is_concrete ? left_type.concrete->kind() : TypeKind::Int;
              const bool is_numeric =
                  is_concrete && (left_type.concrete->is_integer() ||
                                  left_type.concrete->is_floating_point());
              const bool is_orderable =
                  is_numeric || (is_concrete && kind == TypeKind::Char);

              switch (node.operation) {
              case ast::BinaryOperator::Add:
              case ast::BinaryOperator::Subtract:
              case ast::BinaryOperator::Multiply:
              case ast::BinaryOperator::Divide:
                if (!is_numeric) {
                  throw CompileError{
                      node.location,
                      "arithmetic operators require operands of type 'int', "
                      "'byte', 'usize', or 'double'"};
                }
                return left_type;
              case ast::BinaryOperator::Remainder:
                if (!is_concrete || !left_type.concrete->is_integer()) {
                  throw CompileError{node.location,
                                     "operator '%' requires integer operands"};
                }
                return left_type;
              case ast::BinaryOperator::BitwiseAnd:
              case ast::BinaryOperator::BitwiseXor:
              case ast::BinaryOperator::BitwiseOr:
                if (!is_concrete || !left_type.concrete->is_integer())
                  throw CompileError{
                      node.location,
                      "bitwise operators require integer operands"};
                return left_type;
              case ast::BinaryOperator::ShiftLeft:
              case ast::BinaryOperator::ShiftRight:
                return left_type;
              case ast::BinaryOperator::Less:
              case ast::BinaryOperator::LessEqual:
              case ast::BinaryOperator::Greater:
              case ast::BinaryOperator::GreaterEqual:
                if (!is_orderable) {
                  throw CompileError{
                      node.location,
                      "comparison operators require operands of type 'int', "
                      "'byte', 'usize', 'double', or 'char'"};
                }
                return SemanticType{&Type::bool_type(), {}};
              case ast::BinaryOperator::Equal:
              case ast::BinaryOperator::NotEqual:
                if (!is_concrete && !left_type.is_pointer() &&
                    !left_type.is_enum() && !left_type.is_class() &&
                    !supports_derivation(left_type,
                                         ast::DerivationKind::Equality)) {
                  throw CompileError{
                      node.location,
                      "equality operators require primitive operands"};
                }
                if (left_type.is_class() &&
                    !supports_derivation(left_type,
                                         ast::DerivationKind::Equality))
                  throw CompileError{node.location,
                                     "type '" + left_type.name() +
                                         "' does not derive Equality"};
                if (left_type.is_enum()) {
                  const ast::EnumDeclaration &declaration =
                      *enums.at(left_type.parameter);
                  const bool has_payload = std::any_of(
                      declaration.cases.begin(), declaration.cases.end(),
                      [](const ast::EnumDeclaration::Case &enum_case) {
                        return !enum_case.payload_types.empty();
                      });
                  if (has_payload &&
                      !supports_derivation(left_type,
                                           ast::DerivationKind::Equality))
                    throw CompileError{node.location,
                                       "enum '" + left_type.name() +
                                           "' does not derive Equality"};
                }
                return SemanticType{&Type::bool_type(), {}};
              case ast::BinaryOperator::LogicalAnd:
              case ast::BinaryOperator::LogicalOr:
                if (!is_concrete || kind != TypeKind::Bool) {
                  throw CompileError{
                      node.location,
                      "logical operators require operands of type 'bool'"};
                }
                return SemanticType{&Type::bool_type(), {}};
              }
              throw CompileError{node.location, "unsupported binary operator"};
            }
          },
          expression.value);
    };

    const auto statement_location = [](const ast::Statement &statement) {
      return std::visit(
          [](const auto &node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node,
                                         std::shared_ptr<ast::IfStatement>> ||
                          std::is_same_v<
                              Node, std::shared_ptr<ast::WhileStatement>> ||
                          std::is_same_v<Node,
                                         std::shared_ptr<ast::ForStatement>>)
              return node->location;
            else
              return node.location;
          },
          statement);
    };
    const auto is_null_pointer_expression = [](const ast::Expression &value) {
      const auto *call = std::get_if<ast::CallExpression>(&value.value);
      return call != nullptr && call->callee == "null";
    };

    std::function<bool(const std::vector<ast::Statement> &, SymbolTable &)>
        validate_block;
    std::unordered_map<std::string, constant::Value> local_constants;
    validate_block = [&](const std::vector<ast::Statement> &statements,
                         SymbolTable &block_symbols) {
      SymbolTable *previous_symbols = active_symbols;
      const auto previous_local_constants = local_constants;
      const auto previous_deferred_values = deferred_values;
      std::vector<std::pair<std::string, SourceLocation>> scope_declarations;
      active_symbols = &block_symbols;
      bool has_terminator = false;
      for (const ast::Statement &statement : statements) {
        if (has_terminator)
          throw CompileError{statement_location(statement),
                             "unreachable statement after control-flow "
                             "transfer"};

        if (const auto *conditional =
                std::get_if<std::shared_ptr<ast::IfStatement>>(&statement)) {
          validate_expression((*conditional)->condition,
                              SemanticType{&Type::bool_type()},
                              (*conditional)->location);
          SymbolTable then_symbols = block_symbols;
          SymbolTable else_symbols = block_symbols;
          const bool then_returns =
              validate_block((*conditional)->then_body, then_symbols);
          const bool else_returns =
              !(*conditional)->else_body.empty() &&
              validate_block((*conditional)->else_body, else_symbols);
          active_symbols = &block_symbols;
          for (auto &[name, symbol] : block_symbols) {
            if (then_returns && !else_returns) {
              symbol.is_initialized = else_symbols.at(name).is_initialized;
              symbol.may_be_initialized =
                  else_symbols.at(name).may_be_initialized;
            } else if (else_returns && !then_returns) {
              symbol.is_initialized = then_symbols.at(name).is_initialized;
              symbol.may_be_initialized =
                  then_symbols.at(name).may_be_initialized;
            } else {
              symbol.is_initialized = then_symbols.at(name).is_initialized &&
                                      else_symbols.at(name).is_initialized;
              symbol.may_be_initialized =
                  then_symbols.at(name).may_be_initialized ||
                  else_symbols.at(name).may_be_initialized;
            }
          }
          has_terminator = then_returns && else_returns;
          continue;
        }

        if (const auto *loop =
                std::get_if<std::shared_ptr<ast::WhileStatement>>(&statement)) {
          validate_expression((*loop)->condition,
                              SemanticType{&Type::bool_type()},
                              (*loop)->location);
          SymbolTable loop_symbols = block_symbols;
          const auto previous_transfer_protected = transfer_protected_values;
          for (const auto &[name, symbol] : block_symbols) {
            static_cast<void>(symbol);
            transfer_protected_values.insert(name);
          }
          ++loop_depth;
          static_cast<void>(validate_block((*loop)->body, loop_symbols));
          --loop_depth;
          transfer_protected_values = previous_transfer_protected;
          active_symbols = &block_symbols;
          continue;
        }

        if (const auto *loop =
                std::get_if<std::shared_ptr<ast::ForStatement>>(&statement)) {
          const SemanticType source_type = expression_type((*loop)->iterator);
          std::optional<SemanticType> element_type;
          bool consumes_source = false;
          if (source_type.is_class() && source_type.parameter == "Iterator" &&
              source_type.type_arguments.size() == 1) {
            element_type = source_type.type_arguments.front();
            consumes_source = true;
          } else if (source_type.is_class()) {
            const ast::ClassDeclaration &class_declaration =
                *classes.at(source_type.parameter);
            const auto substitutions =
                class_substitutions(class_declaration, source_type);
            const std::unordered_set<std::string> class_parameters{
                class_declaration.type_parameters.begin(),
                class_declaration.type_parameters.end()};
            for (const ast::TypeReference &implemented :
                 class_declaration.implemented_traits) {
              if (implemented.name != "Iterable")
                continue;
              TraitInstance iterable =
                  resolve_trait(implemented, class_parameters);
              if (iterable.type_arguments.size() == 1)
                element_type =
                    substitute(iterable.type_arguments.front(), substitutions);
            }
          } else if (const auto constraint =
                         active_trait_constraints.find(source_type.parameter);
                     constraint != active_trait_constraints.end()) {
            for (const TraitInstance &active : constraint->second)
              if (active.declaration->name == "Iterable" &&
                  active.type_arguments.size() == 1)
                element_type = active.type_arguments.front();
          }
          if (!element_type.has_value())
            throw CompileError{(*loop)->location,
                               "for requires an Iterator[T] or Iterable[T], "
                               "got '" +
                                   source_type.name() + "'"};
          if (!consumes_source && !satisfies_copy(*element_type))
            throw CompileError{(*loop)->location,
                               "for cannot copy an owned Iterable element; use "
                               "intoIterator to consume the collection"};
          SymbolTable loop_symbols = block_symbols;
          loop_symbols.insert_or_assign((*loop)->binding,
                                        Symbol{*element_type, false, true});
          const auto previous_transfer_protected = transfer_protected_values;
          for (const auto &[name, symbol] : block_symbols) {
            static_cast<void>(symbol);
            transfer_protected_values.insert(name);
          }
          ++loop_depth;
          static_cast<void>(validate_block((*loop)->body, loop_symbols));
          --loop_depth;
          transfer_protected_values = previous_transfer_protected;
          active_symbols = &block_symbols;
          if (consumes_source)
            if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                    &(*loop)->iterator.value)) {
              if (deferred_values.contains(identifier->name))
                throw CompileError{(*loop)->location,
                                   "owning value '" + identifier->name +
                                       "' is scheduled for deferred cleanup"};
              block_symbols.at(identifier->name).is_initialized = false;
              block_symbols.at(identifier->name).may_be_initialized = false;
            }
          continue;
        }

        if (const auto *declaration =
                std::get_if<ast::ValueDeclaration>(&statement)) {
          SemanticType declared_type;
          if (declaration->declared_type) {
            declared_type = resolve_type(*declaration->declared_type,
                                         type_parameters, &class_arities,
                                         context_module, &scoped_type_aliases);
          } else {
            try {
              declared_type = expression_type(*declaration->initializer);
            } catch (const CompileError &error) {
              throw CompileError{
                  error.diagnostic().code, declaration->location,
                  "cannot infer type of '" + declaration->name +
                      "'; help: add an explicit type annotation; note: " +
                      error.what()};
            }
          }
          if (declared_type.is_concrete() &&
              declared_type.concrete->kind() == TypeKind::Unit)
            throw CompileError{declaration->location,
                               "Unit cannot be used as a value type"};
          if (block_symbols.contains(declaration->name))
            throw CompileError{declaration->location,
                               "value '" + declaration->name +
                                   "' is already declared"};
          if (declaration->is_borrowed && !declared_type.is_pointer() &&
              (!declaration->initializer.has_value() ||
               !std::holds_alternative<ast::IdentifierExpression>(
                   declaration->initializer->value)))
            throw CompileError{declaration->location,
                               "borrowing an owning value requires a local "
                               "value identifier"};
          if (declaration->is_borrowed &&
              declaration->initializer.has_value() &&
              std::holds_alternative<ast::MoveExpression>(
                  declaration->initializer->value))
            throw CompileError{declaration->location,
                               "a borrowed local cannot move its initializer"};
          if (declaration->initializer.has_value() &&
              declaration->declared_type.has_value()) {
            const bool previous_contextual_borrow_expression =
                contextual_borrow_expression;
            contextual_borrow_expression = declaration->is_borrowed;
            validate_expression(*declaration->initializer, declared_type,
                                declaration->location);
            contextual_borrow_expression =
                previous_contextual_borrow_expression;
          }
          if (declaration->is_constant) {
            if (!declaration->initializer.has_value() ||
                declared_type.concrete == nullptr)
              throw CompileError{
                  declaration->location,
                  "local const requires an initialized scalar type"};
            try {
              constant::Value local_value = constant::evaluate(
                  *declaration->initializer, declared_type.concrete,
                  [&](const std::optional<std::string> &module,
                      std::string_view name,
                      SourceLocation) -> std::optional<constant::Value> {
                    if (!module.has_value()) {
                      const auto local =
                          local_constants.find(std::string{name});
                      if (local != local_constants.end())
                        return local->second;
                    }
                    std::string key = global_key(module, name);
                    if (!module.has_value() && !globals.contains(key)) {
                      if (const auto exported =
                              public_globals.find(std::string{name});
                          exported != public_globals.end())
                        key = exported->second;
                    }
                    if (!globals.contains(key) ||
                        !globals.at(key).declaration->declaration.is_constant)
                      return std::nullopt;
                    return evaluate_global(key);
                  },
                  {}, evaluate_constant_function, &evaluation_budget);
              local_constants.insert_or_assign(declaration->name, local_value);
              result.local_constant_values.insert_or_assign(
                  declaration, std::move(local_value));
            } catch (const CompileError &error) {
              throw CompileError{
                  declaration->location,
                  "local constant '" + declaration->name +
                      "' is not a constant expression: " + error.what()};
            }
          }
          block_symbols.emplace(declaration->name,
                                Symbol{declared_type, declaration->is_mutable,
                                       declaration->initializer.has_value()});
          result.local_types.insert_or_assign(declaration, declared_type);
          if (declaration->initializer.has_value() &&
              is_null_pointer_expression(*declaration->initializer))
            block_symbols.at(declaration->name).may_be_initialized = false;
          if (declaration->is_borrowed ||
              (declaration->initializer.has_value() &&
               is_borrowed_pointer_expression(*declaration->initializer)))
            borrowed_values.insert(declaration->name);
          if (declaration->is_borrowed)
            shared_borrow_values.insert(declaration->name);
          if (declaration->initializer.has_value()) {
            const ast::Expression &initializer = *declaration->initializer;
            if (declaration->is_borrowed)
              if (const auto *source = std::get_if<ast::IdentifierExpression>(
                      &initializer.value))
                borrow_sources[declaration->name].insert(source->name);

            if (const auto *construction =
                    std::get_if<ast::NewExpression>(&initializer.value)) {
              const auto class_iterator = find_in_context(
                  classes, context_module, construction->class_name);
              if (class_iterator != classes.end()) {
                const ast::ClassDeclaration &class_declaration =
                    *class_iterator->second;
                const std::size_t parameter_count =
                    class_declaration.constructor_parameters.size();
                for (std::size_t index = 0;
                     index < class_declaration.constructor_fields.size();
                     ++index) {
                  const ast::ValueDeclaration &field =
                      class_declaration.constructor_fields[index];
                  if (!field.is_borrowed)
                    continue;
                  const ast::Expression &argument =
                      *construction->arguments[parameter_count + index];
                  if (const auto *source =
                          std::get_if<ast::IdentifierExpression>(
                              &argument.value))
                    borrow_sources[declaration->name].insert(source->name);
                }
              }
            }

            if (const auto *move =
                    std::get_if<ast::MoveExpression>(&initializer.value))
              if (const auto *source = std::get_if<ast::IdentifierExpression>(
                      &move->operand->value)) {
                if (const auto borrowed = borrow_sources.find(source->name);
                    borrowed != borrow_sources.end()) {
                  borrow_sources[declaration->name] =
                      std::move(borrowed->second);
                  borrow_sources.erase(borrowed);
                }
                for (auto &[borrower, sources] : borrow_sources) {
                  static_cast<void>(borrower);
                  if (sources.erase(source->name) != 0)
                    sources.insert(declaration->name);
                }
              }
          }
          if (declaration->initializer.has_value())
            if (const auto *lambda = std::get_if<ast::LambdaExpression>(
                    &declaration->initializer->value))
              local_lambda_locations.insert_or_assign(declaration->name,
                                                      lambda->location.offset);
          local_declarations.insert_or_assign(declaration->name,
                                              declaration->location);
          scope_declarations.emplace_back(declaration->name,
                                          declaration->location);
          continue;
        }

        if (const auto *assignment =
                std::get_if<ast::AssignmentStatement>(&statement)) {
          if (!assignment->object.empty()) {
            if (shared_borrow_values.contains(assignment->object))
              throw CompileError{assignment->location,
                                 "cannot mutate through shared borrow '" +
                                     assignment->object + "'"};
            require_no_live_borrow(assignment->object, assignment->location,
                                   "mutated");
            if (!block_symbols.contains(assignment->object) &&
                global_modules.contains(assignment->object)) {
              const ResolvedGlobal *global =
                  find_global(std::optional<std::string>{assignment->object},
                              assignment->name);
              if (global == nullptr)
                throw CompileError{assignment->location,
                                   "module '" + assignment->object +
                                       "' has no global value '" +
                                       assignment->name + "'"};
              if (global->declaration->declaration.is_private &&
                  global->declaration->module_name != context_module)
                throw CompileError{assignment->location,
                                   "global value '" + assignment->object + "." +
                                       assignment->name + "' is private"};
              const std::string spelling =
                  assignment->object + "." + assignment->name;
              if (!import_allows(
                      context_module, global->declaration->module_name,
                      global->declaration->declaration.name, spelling))
                throw CompileError{assignment->location,
                                   "global value '" + spelling +
                                       "' is not imported in this module"};
              if (!global->symbol.is_mutable)
                throw CompileError{assignment->location,
                                   "cannot assign to immutable global value '" +
                                       assignment->object + "." +
                                       assignment->name + "'"};
              result.qualified_global_writes.insert_or_assign(
                  assignment,
                  global_key(global->declaration->module_name,
                             global->declaration->declaration.name));
              validate_expression(assignment->expression, global->symbol.type,
                                  assignment->location);
              continue;
            }
            const auto object = block_symbols.find(assignment->object);
            if (object == block_symbols.end() ||
                !object->second.type.is_class())
              throw CompileError{assignment->location,
                                 "field assignment requires an object"};
            if (!object->second.is_initialized)
              throw CompileError{assignment->location, "object '" +
                                                           assignment->object +
                                                           "' is not alive"};
            const ast::ClassDeclaration &class_declaration =
                *classes.at(object->second.type.parameter);
            const auto substitutions =
                class_substitutions(class_declaration, object->second.type);
            const std::unordered_set<std::string> class_parameters{
                class_declaration.type_parameters.begin(),
                class_declaration.type_parameters.end()};
            const ast::ValueDeclaration *matched = nullptr;
            for (const auto &field : class_declaration.constructor_fields)
              if (field.name == assignment->name)
                matched = &field;
            for (const auto &field : class_declaration.fields)
              if (field.name == assignment->name)
                matched = &field;
            if (matched == nullptr)
              throw CompileError{assignment->location,
                                 "class '" + class_declaration.name +
                                     "' has no field '" + assignment->name +
                                     "'"};
            if (matched->is_private &&
                (owner == nullptr || owner->name != class_declaration.name))
              throw CompileError{assignment->location,
                                 "field '" + assignment->name +
                                     "' is private in class '" +
                                     class_declaration.name + "'"};
            if (matched->is_internal &&
                class_declaration.module_name != context_module)
              throw CompileError{
                  assignment->location,
                  "field '" + assignment->name + "' is internal to module '" +
                      class_declaration.module_name.value_or("<entry>") + "'"};
            if (!matched->is_mutable)
              throw CompileError{assignment->location,
                                 "cannot assign to immutable field '" +
                                     assignment->name + "'"};
            const SemanticType field_type =
                substitute(resolve_type(*matched->declared_type,
                                        class_parameters, &class_arities),
                           substitutions);
            validate_expression(assignment->expression, field_type,
                                assignment->location);
            if (potentially_owns_value(field_type))
              emit_warning(
                  DiagnosticCode::AnalyzerOwningFieldOverwritten,
                  assignment->location,
                  "assignment to field '" + assignment->object + "." +
                      assignment->name +
                      "' may overwrite a live owning value of type '" +
                      field_type.name() + "'",
                  {"delete or move the current field value before assigning "
                   "its replacement"});
            continue;
          }
          const auto iterator = block_symbols.find(assignment->name);
          if (iterator == block_symbols.end()) {
            const Symbol *global = visible_global(assignment->name);
            if (global == nullptr)
              throw CompileError{DiagnosticCode::AnalyzerUnknownValue,
                                 assignment->location,
                                 "unknown value '" + assignment->name + "'"};
            if (!global->is_mutable)
              throw CompileError{assignment->location,
                                 "cannot assign to immutable global value '" +
                                     assignment->name + "'"};
            validate_expression(assignment->expression, global->type,
                                assignment->location);
            continue;
          }
          require_no_live_borrow(assignment->name, assignment->location,
                                 "overwritten");
          if (deferred_values.contains(assignment->name))
            throw CompileError{assignment->location,
                               "owning value '" + assignment->name +
                                   "' is scheduled for deferred cleanup"};
          if (!iterator->second.is_mutable)
            throw CompileError{assignment->location,
                               "cannot assign to immutable value '" +
                                   assignment->name + "'"};
          const bool may_overwrite_owner =
              report_local_warnings && iterator->second.may_be_initialized &&
              potentially_owns_value(iterator->second.type);
          bool self_reallocation = false;
          if (const auto *call = std::get_if<ast::CallExpression>(
                  &assignment->expression.value);
              call != nullptr && call->callee == "realloc" &&
              !call->arguments.empty())
            if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                    &call->arguments.front()->value))
              self_reallocation = identifier->name == assignment->name;
          bool reinitializes_moved_loop_owner = false;
          if (loop_depth != 0)
            if (const auto *call = std::get_if<ast::CallExpression>(
                    &assignment->expression.value))
              reinitializes_moved_loop_owner = std::any_of(
                  call->arguments.begin(), call->arguments.end(),
                  [&](const std::unique_ptr<ast::Expression> &argument) {
                    const auto *move =
                        std::get_if<ast::MoveExpression>(&argument->value);
                    if (move == nullptr)
                      return false;
                    const auto *identifier =
                        std::get_if<ast::IdentifierExpression>(
                            &move->operand->value);
                    return identifier != nullptr &&
                           identifier->name == assignment->name;
                  });
          const bool was_transfer_protected =
              transfer_protected_values.contains(assignment->name);
          if (reinitializes_moved_loop_owner)
            transfer_protected_values.erase(assignment->name);
          validate_expression(assignment->expression, iterator->second.type,
                              assignment->location);
          if (was_transfer_protected)
            transfer_protected_values.insert(assignment->name);
          if (self_reallocation)
            emit_warning(
                DiagnosticCode::AnalyzerUnsafeReallocation,
                assignment->location,
                "assigning realloc directly back to '" + assignment->name +
                    "' can lose the original allocation when resizing fails",
                {"store the realloc result in a temporary pointer, check it "
                 "against null, then replace the original pointer"});
          if (may_overwrite_owner && iterator->second.may_be_initialized &&
              !self_reallocation) {
            const DiagnosticCode code =
                owner_field_names.contains(assignment->name)
                    ? DiagnosticCode::AnalyzerOwningFieldOverwritten
                    : DiagnosticCode::AnalyzerOwningValueOverwritten;
            result.diagnostics.push_back(Diagnostic{
                DiagnosticSeverity::Warning,
                code,
                "assignment to '" + assignment->name +
                    "' may overwrite a live owning value of type '" +
                    iterator->second.type.name() + "'",
                assignment->location,
                {"delete or move the current value before assigning its "
                 "replacement"},
                {},
                {},
            });
          }
          iterator->second.is_initialized = true;
          iterator->second.may_be_initialized =
              !is_null_pointer_expression(assignment->expression);
          if (is_borrowed_pointer_expression(assignment->expression))
            borrowed_values.insert(assignment->name);
          else
            borrowed_values.erase(assignment->name);
          continue;
        }

        if (const auto *deletion =
                std::get_if<ast::DeleteStatement>(&statement)) {
          require_guard_transfer_allowed(deletion->expression,
                                         deletion->location);
          if (!std::holds_alternative<ast::IdentifierExpression>(
                  deletion->expression.value) &&
              is_borrowed_pointer_expression(deletion->expression))
            throw CompileError{deletion->location,
                               "borrowed pointer cannot be deleted"};
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value);
              identifier != nullptr &&
              deferred_values.contains(identifier->name))
            throw CompileError{deletion->location,
                               "owning value '" + identifier->name +
                                   "' is scheduled for deferred cleanup"};
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value);
              identifier != nullptr &&
              borrowed_values.contains(identifier->name))
            throw CompileError{deletion->location, "borrowed value '" +
                                                       identifier->name +
                                                       "' cannot be deleted"};
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value))
            require_no_live_borrow(identifier->name, deletion->location);
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value);
              identifier != nullptr &&
              !block_symbols.contains(identifier->name) &&
              visible_global(identifier->name) != nullptr)
            throw CompileError{deletion->location,
                               "owning global value '" + identifier->name +
                                   "' is destroyed automatically"};
          const SemanticType deleted_type =
              expression_type(deletion->expression);
          const bool is_struct =
              deleted_type.is_class() &&
              classes.at(deleted_type.parameter)->is_value_type;
          if (is_struct && !aggregate_owns_value(deleted_type))
            throw CompileError{deletion->location,
                               "struct values do not require delete"};
          if (!deleted_type.is_class() && !deleted_type.is_function() &&
              !potentially_owns_value(deleted_type) &&
              !(deleted_type.is_enum() && aggregate_owns_value(deleted_type)))
            throw CompileError{deletion->location,
                               "delete requires an object or a function value"};
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value))
            block_symbols.at(identifier->name).is_initialized = false;
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value))
            block_symbols.at(identifier->name).may_be_initialized = false;
          continue;
        }

        if (const auto *deferred =
                std::get_if<ast::DeferStatement>(&statement)) {
          SymbolTable deferred_symbols = block_symbols;
          SymbolTable *previous_deferred_symbols = active_symbols;
          active_symbols = &deferred_symbols;
          if (const auto *deletion =
                  std::get_if<ast::DeleteStatement>(&deferred->action)) {
            const auto *identifier = std::get_if<ast::IdentifierExpression>(
                &deletion->expression.value);
            if (identifier == nullptr)
              throw CompileError{
                  deletion->location,
                  "deferred delete requires an owning local identifier"};
            if (!block_symbols.contains(identifier->name) &&
                visible_global(identifier->name) != nullptr)
              throw CompileError{deletion->location,
                                 "owning global value '" + identifier->name +
                                     "' is destroyed automatically"};
            if (deferred_values.contains(identifier->name))
              throw CompileError{
                  deletion->location,
                  "owning value '" + identifier->name +
                      "' is already scheduled for deferred cleanup"};
            const SemanticType deleted_type =
                expression_type(deletion->expression);
            const bool is_struct =
                deleted_type.is_class() &&
                classes.at(deleted_type.parameter)->is_value_type;
            if (is_struct && !aggregate_owns_value(deleted_type))
              throw CompileError{deletion->location,
                                 "struct values do not require delete"};
            if (!deleted_type.is_class() && !deleted_type.is_function() &&
                !potentially_owns_value(deleted_type) &&
                !(deleted_type.is_enum() && aggregate_owns_value(deleted_type)))
              throw CompileError{
                  deletion->location,
                  "deferred delete requires an object or a function value"};
            deferred_values.insert(identifier->name);
          } else {
            const auto &action =
                std::get<ast::ExpressionStatement>(deferred->action);
            const bool previous_inside_defer = inside_defer;
            inside_defer = true;
            static_cast<void>(expression_type(action.expression));
            inside_defer = previous_inside_defer;
            for (const auto &[name, symbol] : block_symbols) {
              const auto deferred_symbol = deferred_symbols.find(name);
              if (symbol.is_initialized &&
                  deferred_symbol != deferred_symbols.end() &&
                  !deferred_symbol->second.is_initialized) {
                if (deferred_values.contains(name))
                  throw CompileError{
                      deferred->location,
                      "owning value '" + name +
                          "' is already scheduled for deferred cleanup"};
                deferred_values.insert(name);
              }
            }
          }
          active_symbols = previous_deferred_symbols;
          continue;
        }

        if (const auto *jump = std::get_if<ast::BreakStatement>(&statement)) {
          if (loop_depth == 0)
            throw CompileError{jump->location,
                               "break can only be used inside a loop"};
          has_terminator = true;
          continue;
        }

        if (const auto *jump =
                std::get_if<ast::ContinueStatement>(&statement)) {
          if (loop_depth == 0)
            throw CompileError{jump->location,
                               "continue can only be used inside a loop"};
          has_terminator = true;
          continue;
        }

        if (const auto *expression_statement =
                std::get_if<ast::ExpressionStatement>(&statement)) {
          if (!std::holds_alternative<ast::CallExpression>(
                  expression_statement->expression.value) &&
              !std::holds_alternative<ast::MethodCallExpression>(
                  expression_statement->expression.value))
            throw CompileError{
                expression_statement->location,
                "only function and method calls can be used as statements"};
          const SemanticType discarded_type =
              expression_type(expression_statement->expression);
          const bool must_use_result = discarded_type.is_enum() &&
                                       (discarded_type.parameter == "Option" ||
                                        discarded_type.parameter == "Result");
          if (must_use_result)
            emit_warning(
                DiagnosticCode::AnalyzerMustUseResult,
                expression_statement->location,
                "result of type '" + discarded_type.name() +
                    "' must be handled or explicitly stored",
                {"match the result, propagate it with '?', or bind it to a "
                 "named value"});
          else if (report_local_warnings &&
                   potentially_owns_value(discarded_type) &&
                   !is_borrowed_pointer_expression(
                       expression_statement->expression) &&
                   !is_null_pointer_expression(
                       expression_statement->expression)) {
            result.diagnostics.push_back(Diagnostic{
                DiagnosticSeverity::Warning,
                DiagnosticCode::AnalyzerOwningResultDiscarded,
                "call result has owning type '" + discarded_type.name() +
                    "' and is discarded",
                expression_statement->location,
                {"bind the result and delete or move it if ownership was "
                 "transferred; borrowed results need an ownership-explicit "
                 "API"},
                {},
                {},
            });
          }
          if (const auto *call = std::get_if<ast::CallExpression>(
                  &expression_statement->expression.value);
              call != nullptr && call->callee == "panic") {
            for (const auto &[name, declaration] : local_declarations) {
              const auto symbol = block_symbols.find(name);
              if (symbol == block_symbols.end() ||
                  !symbol->second.may_be_initialized ||
                  deferred_values.contains(name) ||
                  !potentially_owns_value(symbol->second.type))
                continue;
              const std::string cleanup = symbol->second.type.is_pointer()
                                              ? "defer free(" + name + ")"
                                              : "defer delete " + name;
              emit_warning(
                  DiagnosticCode::AnalyzerUnprotectedPanic,
                  expression_statement->location,
                  "panic may terminate while owning value '" + name +
                      "' is not protected by deferred cleanup",
                  {"schedule cleanup before this call with '" + cleanup + "'"},
                  {DiagnosticLocation{declaration,
                                      "owning value declared here"}});
              warned_leak_locations.insert(declaration.offset);
            }
            has_terminator = true;
          }
          continue;
        }

        const auto &return_statement =
            std::get<ast::ReturnStatement>(statement);
        if (return_type.is_concrete() &&
            return_type.concrete->kind() == TypeKind::Unit) {
          if (return_statement.expression.has_value())
            throw CompileError{return_statement.location,
                               "a Unit function cannot return a value"};
        } else {
          if (!return_statement.expression.has_value())
            throw CompileError{return_statement.location,
                               "return requires a value of type '" +
                                   return_type.name() + "'"};
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &return_statement.expression->value);
              identifier != nullptr &&
              deferred_values.contains(identifier->name))
            throw CompileError{return_statement.location,
                               "owning value '" + identifier->name +
                                   "' is scheduled for deferred cleanup"};
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &return_statement.expression->value);
              identifier != nullptr &&
              borrowed_values.contains(identifier->name) &&
              potentially_owns_value(return_type))
            throw CompileError{return_statement.location,
                               "borrowed value '" + identifier->name +
                                   "' cannot escape by return"};
          validate_return_expression(return_statement);
          std::optional<std::size_t> returned_lambda_location;
          if (const auto *lambda = std::get_if<ast::LambdaExpression>(
                  &return_statement.expression->value))
            returned_lambda_location = lambda->location.offset;
          else if (const auto *identifier =
                       std::get_if<ast::IdentifierExpression>(
                           &return_statement.expression->value);
                   identifier != nullptr)
            if (const auto stored =
                    local_lambda_locations.find(identifier->name);
                stored != local_lambda_locations.end())
              returned_lambda_location = stored->second;
          if (returned_lambda_location.has_value()) {
            const auto captures =
                lambda_captures.find(*returned_lambda_location);
            if (captures != lambda_captures.end())
              for (const std::string &capture : captures->second) {
                const auto symbol = block_symbols.find(capture);
                const auto declaration = local_declarations.find(capture);
                if (symbol == block_symbols.end() ||
                    declaration == local_declarations.end() ||
                    !potentially_owns_value(symbol->second.type))
                  continue;
                emit_warning(
                    DiagnosticCode::AnalyzerEscapingOwningCapture,
                    return_statement.location,
                    "returned closure captures owning value '" + capture +
                        "' with no explicit transfer contract",
                    {"move ownership into an explicit wrapper or keep the "
                     "closure within the owner's scope"},
                    {DiagnosticLocation{declaration->second,
                                        "captured owner declared here"}});
              }
          }
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &return_statement.expression->value);
              identifier != nullptr &&
              local_declarations.contains(identifier->name) &&
              potentially_owns_value(return_type)) {
            block_symbols.at(identifier->name).is_initialized = false;
            block_symbols.at(identifier->name).may_be_initialized = false;
          }
        }
        warn_all_live_owners(block_symbols);
        has_terminator = true;
      }
      for (const std::string &name : deferred_values) {
        if (previous_deferred_values.contains(name))
          continue;
        const auto symbol = block_symbols.find(name);
        if (symbol != block_symbols.end()) {
          symbol->second.is_initialized = false;
          symbol->second.may_be_initialized = false;
        }
      }
      for (const auto &[name, location] : scope_declarations) {
        const auto symbol = block_symbols.find(name);
        if (symbol == block_symbols.end())
          continue;
        const bool owns_value = potentially_owns_value(symbol->second.type);
        const bool loop_leak = loop_depth > 0 &&
                               symbol->second.may_be_initialized &&
                               owns_value && !deferred_values.contains(name);
        if (loop_leak)
          emit_warning(
              DiagnosticCode::AnalyzerLoopAllocation, location,
              "owning value '" + name + "' of type '" +
                  symbol->second.type.name() +
                  "' may survive an iteration and leak repeatedly",
              {"release, defer, or move the value before the next loop "
               "iteration"});
        else
          warn_live_owner(name, symbol->second, location);

        if (!owns_value && !name.starts_with('_') &&
            !used_local_declarations.contains(location.offset))
          emit_warning(DiagnosticCode::AnalyzerUnusedValue, location,
                       "value '" + name + "' is declared but never used",
                       {"remove it or prefix its name with '_' to document "
                        "intentional non-use"});
      }
      for (const auto &[name, location] : scope_declarations) {
        static_cast<void>(location);
        local_declarations.erase(name);
        local_lambda_locations.erase(name);
        borrowed_values.erase(name);
        shared_borrow_values.erase(name);
        borrow_sources.erase(name);
      }
      active_symbols = previous_symbols;
      local_constants = previous_local_constants;
      deferred_values = previous_deferred_values;
      return has_terminator;
    };

    if (is_global_initializer) {
      const std::string key = global_key(context.global->module_name,
                                         context.global->declaration.name);
      validate_expression(*context.global->declaration.initializer,
                          globals.at(key).symbol.type,
                          context.global->declaration.location);
      continue;
    }

    static_cast<void>(validate_block(body, symbols));
    if (is_destructor && owner != nullptr) {
      const auto check_owned_field = [&](const ast::ValueDeclaration &field) {
        if (field.is_borrowed)
          return;
        const auto symbol = symbols.find(field.name);
        if (symbol == symbols.end() || !symbol->second.may_be_initialized ||
            !potentially_owns_value(symbol->second.type))
          return;
        emit_warning(
            DiagnosticCode::AnalyzerIncompleteDestructor, field.location,
            "destructor for '" + owner->name + "' may leave owning field '" +
                field.name + "' of type '" + symbol->second.type.name() +
                "' alive",
            {"delete, free, defer, or move the field on every destructor "
             "path"});
      };
      for (const ast::ValueDeclaration &field : owner->constructor_fields)
        check_owned_field(field);
      for (const ast::ValueDeclaration &field : owner->fields)
        check_owned_field(field);
    }
    const bool has_return = block_guarantees_return(body);

    if (!has_return && (!return_type.is_concrete() ||
                        return_type.concrete->kind() != TypeKind::Unit)) {
      throw CompileError{
          function_location,
          "function '" + function_name +
              "' must return a value of expected return type '" +
              return_type.name() +
              "', but not all control-flow paths do; add a return statement "
              "returning '" +
              return_type.name() + "' on every path"};
    }
    const std::string analysis_name =
        owner == nullptr ? function_name : owner->name + "." + function_name;
    result.functions.emplace(analysis_name, std::move(symbols));
  }

  return result;
}

} // namespace janus::semantic
