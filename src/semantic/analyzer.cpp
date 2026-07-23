#include "janus/semantic/analyzer.hpp"

#include "janus/constant/evaluator.hpp"
#include "janus/diagnostics/compile_error.hpp"

#include <algorithm>
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

janus::semantic::SemanticType
resolve_type(const janus::ast::TypeReference &reference,
             const std::unordered_set<std::string> &type_parameters,
             const std::unordered_map<std::string, std::size_t> *class_arities =
                 nullptr) {
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
      signature.push_back(
          resolve_type(argument, type_parameters, class_arities));
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
    janus::semantic::SemanticType element = resolve_type(
        reference.type_arguments.front(), type_parameters, class_arities);
    if (element.is_concrete() &&
        element.concrete->kind() == janus::TypeKind::Unit)
      throw janus::CompileError{reference.location,
                                "Ptr[Unit] is not a valid pointer type"};
    return janus::semantic::SemanticType{
        nullptr, "Ptr", false, {std::move(element)}, true};
  }
  if (class_arities != nullptr) {
    if (const auto iterator = class_arities->find(reference.name);
        iterator != class_arities->end()) {
      if (iterator->second == ambiguous_arity_marker)
        throw janus::CompileError{
            reference.location,
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
          arguments.push_back(
              resolve_type(argument, type_parameters, class_arities));
        return janus::semantic::SemanticType{
            nullptr, reference.name, false, std::move(arguments), false, true};
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
        arguments.push_back(
            resolve_type(argument, type_parameters, class_arities));
      }
      return janus::semantic::SemanticType{nullptr, reference.name, true,
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

bool is_c_abi_type(const janus::semantic::SemanticType &type,
                   bool allow_unit) {
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
    const __int128 magnitude =
        static_cast<__int128>(literal->magnitude);
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
  std::unordered_map<std::string, const ast::FunctionDeclaration *> functions;
  std::unordered_map<std::string, const ast::ClassDeclaration *> classes;
  std::unordered_map<std::string, const ast::EnumDeclaration *> enums;
  std::unordered_map<std::string, const ast::TraitDeclaration *> traits;
  std::unordered_map<std::string, std::size_t> class_arities;
  std::unordered_map<std::string, std::size_t> type_name_counts;
  std::unordered_set<std::string> type_identities;
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
    class_arities.emplace(
        identity,
        enum_arity_marker + declaration.type_parameters.size());
  }
  for (const ast::TraitDeclaration &declaration : program.traits) {
    const std::string identity = register_type_identity(
        declaration.module_name, declaration.name, declaration.location);
    traits.emplace(identity, &declaration);
  }
  for (const ast::ClassDeclaration &declaration : program.classes) {
    const std::string identity = register_type_identity(
        declaration.module_name, declaration.name, declaration.location);
    classes.emplace(identity, &declaration);
    class_arities.emplace(identity, declaration.type_parameters.size());
  }
  for (const ast::EnumDeclaration &declaration : program.enums)
    if (type_name_counts.at(declaration.name) == 1) {
      enums.emplace(declaration.name, &declaration);
      class_arities.emplace(
          declaration.name,
          enum_arity_marker + declaration.type_parameters.size());
    } else {
      class_arities.insert_or_assign(declaration.name,
                                     ambiguous_arity_marker);
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
      class_arities.insert_or_assign(declaration.name,
                                     ambiguous_arity_marker);
    }

  struct TypeVisibility {
    bool is_private;
    std::optional<std::string> module;
  };
  std::unordered_map<std::string, TypeVisibility> type_visibility;
  for (const ast::EnumDeclaration &declaration : program.enums)
    type_visibility.emplace(
        global_key(declaration.module_name, declaration.name),
        TypeVisibility{declaration.is_private, declaration.module_name});
  for (const ast::TraitDeclaration &declaration : program.traits)
    type_visibility.emplace(
        global_key(declaration.module_name, declaration.name),
        TypeVisibility{declaration.is_private, declaration.module_name});
  for (const ast::ClassDeclaration &declaration : program.classes)
    type_visibility.emplace(
        global_key(declaration.module_name, declaration.name),
        TypeVisibility{declaration.is_private, declaration.module_name});
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
        visibility != type_visibility.end() &&
        visibility->second.is_private &&
        visibility->second.module != context_module)
      throw CompileError{reference.location,
                         "type '" + identity + "' is private"};
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
                          global.declaration.declared_type,
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
    for (const ast::TypeReference &implemented :
         declaration.implemented_traits)
      check_type_visibility(check_type_visibility, implemented,
                            declaration.module_name);
    for (const ast::FunctionDeclaration::Parameter &parameter :
         declaration.constructor_parameters)
      check_type_visibility(check_type_visibility, parameter.type,
                            declaration.module_name);
    for (const ast::ValueDeclaration &field :
         declaration.constructor_fields)
      check_type_visibility(check_type_visibility, field.declared_type,
                            declaration.module_name);
    for (const ast::ValueDeclaration &field : declaration.fields)
      check_type_visibility(check_type_visibility, field.declared_type,
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
  std::function<bool(const SemanticType &)> aggregate_owns_value;
  aggregate_owns_value = [&](const SemanticType &type) {
    if (type.is_function() || type.is_pointer())
      return true;
    if (type.is_class()) {
      const ast::ClassDeclaration &declaration = *classes.at(type.parameter);
      if (!declaration.is_value_type)
        return true;
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
        SemanticType field_type =
            resolve_type(field.declared_type, parameters, &class_arities);
        if (aggregate_owns_value(
                substitute(std::move(field_type), substitutions)))
          return true;
      }
      return false;
    }
    if (type.is_enum()) {
      const ast::EnumDeclaration &declaration = *enums.at(type.parameter);
      const std::unordered_set<std::string> parameters{
          declaration.type_parameters.begin(),
          declaration.type_parameters.end()};
      std::unordered_map<std::string, SemanticType> substitutions;
      for (std::size_t index = 0;
           index < declaration.type_parameters.size(); ++index)
        substitutions.emplace(declaration.type_parameters[index],
                              type.type_arguments[index]);
      for (const ast::EnumDeclaration::Case &enum_case : declaration.cases)
        for (const ast::TypeReference &payload : enum_case.payload_types) {
          SemanticType payload_type =
              resolve_type(payload, parameters, &class_arities);
          if (aggregate_owns_value(
                  substitute(std::move(payload_type), substitutions)))
            return true;
        }
    }
    return false;
  };
  for (const ast::GlobalDeclaration &global : program.globals) {
    const ast::ValueDeclaration &declaration = global.declaration;
    const std::string key = global_key(global.module_name, declaration.name);
    if (globals.contains(key))
      throw CompileError{declaration.location,
                         "global value '" + key +
                             "' is already declared"};
    if (!declaration.is_private) {
      if (const auto existing = public_globals.find(declaration.name);
          existing != public_globals.end())
        throw CompileError{
            declaration.location,
            "public global value '" + declaration.name +
                "' is exported by both modules '" + existing->second +
                "' and '" + global.module_name.value_or("<entry>") + "'"};
      public_globals.emplace(declaration.name, key);
    }
    if (global.module_name.has_value())
      global_modules.insert(*global.module_name);
    const SemanticType type = resolve_type(
        declaration.declared_type, no_type_parameters, &class_arities);
    if (type.is_concrete() && type.concrete->kind() == TypeKind::Unit)
      throw CompileError{
          declaration.location,
          "Unit cannot be used as a global value type"};
    const bool owns_value = aggregate_owns_value(type);
    if (owns_value && declaration.is_mutable)
      throw CompileError{
          declaration.location,
          "owning global value '" + declaration.name +
              "' must be declared with 'val'"};
    if (!declaration.initializer.has_value())
      throw CompileError{declaration.location,
                         "global variable '" + declaration.name +
                             "' requires an initializer"};
    Symbol symbol{type, declaration.is_mutable, true};
    result.globals.emplace(key, symbol);
    globals.emplace(key, ResolvedGlobal{&global, std::move(symbol)});
  }

  enum class ConstantState { Unvisited, Visiting, Complete };
  std::unordered_map<std::string, ConstantState> constant_states;
  std::unordered_map<std::string, constant::Value> constant_values;
  const constant::InitializationPlan initialization_plan =
      constant::plan_initialization(program);
  std::function<const constant::Value &(const std::string &)> evaluate_global;
  evaluate_global = [&](const std::string &key) -> const constant::Value & {
    const ConstantState state = constant_states[key];
    if (state == ConstantState::Visiting)
      throw CompileError{
          globals.at(key).declaration->declaration.location,
          "cyclic global constant dependency involving '" + key + "'"};
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
        throw CompileError{location,
                           "global constant '" + dependency_key +
                               "' is private"};
      if (target.declaration.is_mutable)
        throw CompileError{
            location, "global constant initializer cannot depend on mutable "
                      "global '" +
                          dependency_key + "'"};
      return evaluate_global(dependency_key);
    };
    constant::Value value = constant::evaluate(
        *global.declaration.initializer, resolved.symbol.type.concrete,
        resolver);
    constant_states[key] = ConstantState::Complete;
    auto [iterator, inserted] =
        constant_values.emplace(key, std::move(value));
    static_cast<void>(inserted);
    return iterator->second;
  };
  for (const ast::GlobalDeclaration *global : initialization_plan.constants)
    if (globals
            .at(global_key(global->module_name, global->declaration.name))
            .symbol.type.concrete != nullptr)
      static_cast<void>(
          evaluate_global(global_key(global->module_name,
                                     global->declaration.name)));

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
          static_cast<void>(resolve_trait(constraint.trait, type_parameters));
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
        throw CompileError{
            class_declaration.location,
            "struct constructors only support val/var fields"};
      if (!class_declaration.fields.empty())
        throw CompileError{
            class_declaration.location,
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
          resolve_type(field.declared_type, parameters, &class_arities);
      if (field_type.is_concrete() &&
          field_type.concrete->kind() == TypeKind::Unit)
        throw CompileError{field.location,
                           "Unit cannot be used as a field type"};
    }
    for (const ast::ValueDeclaration &field : class_declaration.fields) {
      const SemanticType field_type =
          resolve_type(field.declared_type, parameters, &class_arities);
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
        if (implementation->is_consuming != required.is_consuming)
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
          compatible = same_type(
              required_type(required.parameters[index].type),
              implemented_type(implementation->parameters[index].type));
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
        global.module_name});
    contexts.push_back(FunctionContext{&global_initializer_functions.back(),
                                       nullptr, nullptr, &global});
  }
  std::unordered_map<std::string, const ast::FunctionDeclaration *>
      external_symbols;
  for (const ast::FunctionDeclaration &function : program.functions) {
    if (!function.is_external)
      continue;
    const std::string &symbol =
        function.external_symbol.has_value() ? *function.external_symbol
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
    const std::optional<std::string> &context_module =
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
      throw CompileError{
          function_location,
          "external function '" + function_name + "' cannot be generic"};
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
                                     type_parameters, &class_arities);
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
        symbols.emplace(field.name,
                        Symbol{resolve_type(field.declared_type,
                                            type_parameters, &class_arities),
                               field.is_mutable, true});
      }
      for (const ast::ValueDeclaration &field : owner->fields) {
        owner_field_names.insert(field.name);
        symbols.emplace(field.name,
                        Symbol{resolve_type(field.declared_type,
                                            type_parameters, &class_arities),
                               field.is_mutable,
                               field.initializer.has_value()});
      }
    }
    for (const ast::FunctionDeclaration::Parameter &parameter : parameters) {
      const SemanticType parameter_type =
          resolve_type(parameter.type, type_parameters, &class_arities);
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
    if (!is_destructor && context.function->is_external) {
      if (!is_c_abi_type(return_type, true))
        throw CompileError{
            context.function->return_type.location,
            "external function return type '" + return_type.name() +
                "' is not compatible with the C ABI"};
      for (const ast::FunctionDeclaration::Parameter &parameter : parameters) {
        const SemanticType parameter_type =
            resolve_type(parameter.type, type_parameters, &class_arities);
        if (!is_c_abi_type(parameter_type, false))
          throw CompileError{
              parameter.location,
              "external parameter '" + parameter.name + "' has type '" +
                  parameter_type.name() +
                  "', which is not compatible with the C ABI"};
      }
      result.functions.emplace(function_name, std::move(symbols));
      continue;
    }
    std::unordered_map<std::string, std::vector<TraitInstance>>
        active_trait_constraints;
    const auto add_active_constraints =
        [&](const std::vector<ast::TypeConstraint> &constraints) {
          for (const ast::TypeConstraint &constraint : constraints)
            active_trait_constraints[constraint.parameter].push_back(
                resolve_trait(constraint.trait, type_parameters));
        };
    if (owner != nullptr)
      add_active_constraints(owner->type_constraints);
    if (!is_destructor)
      add_active_constraints(context.function->type_constraints);
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
    SymbolTable *active_symbols = &symbols;
    const auto find_global =
        [&](const std::optional<std::string> &module,
            std::string_view name) -> const ResolvedGlobal * {
      const auto iterator = globals.find(global_key(module, name));
      return iterator == globals.end() ? nullptr : &iterator->second;
    };
    const auto visible_global =
        [&](std::string_view name) -> const Symbol * {
      if (const ResolvedGlobal *local = find_global(context_module, name))
        return &local->symbol;
      const auto exported = public_globals.find(std::string{name});
      if (exported == public_globals.end())
        return nullptr;
      return &globals.at(exported->second).symbol;
    };
    const std::unordered_set<std::string> *active_type_parameters =
        &type_parameters;
    const std::unordered_map<std::string, SemanticType>
        *active_type_substitutions = nullptr;
    bool inside_lambda = false;
    bool inside_defer = false;
    std::size_t loop_depth = 0;
    std::unordered_set<std::string> transfer_protected_values;
    std::unordered_set<std::string> deferred_values;

    std::function<SemanticType(const ast::Expression &)> expression_type;
    std::function<void(const ast::Expression &, const SemanticType &,
                       SourceLocation)>
        validate_expression;
    const auto declared_call_type =
        [&](const ast::FunctionDeclaration &callee,
            const std::vector<ast::TypeReference> &type_arguments,
            const std::vector<std::unique_ptr<ast::Expression>> &arguments,
            SourceLocation location, std::string_view display_name) {
          if (type_arguments.size() != callee.type_parameters.size())
            throw CompileError{
                location, "function '" + std::string{display_name} +
                              "' expects " +
                              std::to_string(callee.type_parameters.size()) +
                              " type argument(s), got " +
                              std::to_string(type_arguments.size())};
          if ((!callee.is_variadic &&
               arguments.size() != callee.parameters.size()) ||
              (callee.is_variadic &&
               arguments.size() < callee.parameters.size()))
            throw CompileError{
                location, "function '" + std::string{display_name} +
                              "' expects " +
                              (callee.is_variadic ? "at least " : "") +
                              std::to_string(callee.parameters.size()) +
                              " argument(s), got " +
                              std::to_string(arguments.size())};

          std::unordered_map<std::string, SemanticType> substitutions;
          for (std::size_t index = 0; index < type_arguments.size(); ++index)
            substitutions.emplace(
                callee.type_parameters[index],
                resolve_type(type_arguments[index], *active_type_parameters,
                             &class_arities));
          const std::unordered_set<std::string> callee_parameters{
              callee.type_parameters.begin(), callee.type_parameters.end()};
          for (const ast::TypeConstraint &constraint :
               callee.type_constraints) {
            TraitInstance requirement =
                resolve_trait(constraint.trait, callee_parameters);
            for (SemanticType &argument : requirement.type_arguments)
              argument = substitute(std::move(argument), substitutions);
            const SemanticType &candidate =
                substitutions.at(constraint.parameter);
            if (!satisfies_active_trait(candidate, requirement))
              throw CompileError{
                  location, "type '" + candidate.name() +
                                "' does not satisfy constraint '" +
                                requirement.declaration->name +
                                "' for type parameter '" +
                                constraint.parameter + "'"};
          }
          for (std::size_t index = 0; index < arguments.size(); ++index) {
            if (index >= callee.parameters.size()) {
              const SemanticType argument_type =
                  expression_type(*arguments[index]);
              if (!is_c_variadic_type(argument_type))
                throw CompileError{
                    expression_location(*arguments[index]),
                    "variadic C argument has incompatible type '" +
                        argument_type.name() + "'"};
              continue;
            }
            SemanticType expected =
                resolve_type(callee.parameters[index].type, callee_parameters,
                             &class_arities);
            expected = substitute(std::move(expected), substitutions);
            validate_expression(*arguments[index], expected,
                                expression_location(*arguments[index]));
          }
          return substitute(resolve_type(callee.return_type, callee_parameters,
                                         &class_arities),
                            substitutions);
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
          throw CompileError{
              expression_location(expression),
              "integer literal is outside the " +
              integer_range_description(*expected.concrete)};
        }
      }

      const SemanticType actual = expression_type(expression);
      if (same_type(actual, expected))
        return;

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

          const SemanticType actual = expression_type(expression);
          if (same_type(actual, return_type))
            return;

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
              return SemanticType{&Type::double_type(), {}};
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
                                                ast::IdentifierExpression>) {
              const auto iterator = active_symbols->find(node.name);
              if (iterator == active_symbols->end()) {
                if (const Symbol *global = visible_global(node.name))
                  return global->type;
                throw CompileError{node.location,
                                   "unknown value '" + node.name + "'"};
              }
              if (!iterator->second.is_initialized) {
                throw CompileError{node.location,
                                   "variable '" + node.name +
                                       "' is used before initialization"};
              }
              return iterator->second.type;
            } else if constexpr (std::is_same_v<Node, ast::LambdaExpression>) {
              SymbolTable lambda_symbols = *active_symbols;
              std::unordered_set<std::string> parameter_names;
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
              }
              SymbolTable *previous_symbols = active_symbols;
              active_symbols = &lambda_symbols;
              const bool previous_inside_lambda = inside_lambda;
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
              transfer_protected_values = previous_transfer_protected;
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
                          " supports int, double, byte, char, bool, string, and "
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
                return SemanticType{
                    nullptr,
                    "Ptr",
                    false,
                    {SemanticType{&Type::byte_type()}},
                    true};
              }
              if (node.callee == "alloc" || node.callee == "realloc" ||
                  node.callee == "null" || node.callee == "sizeof" ||
                  node.callee == "alignof") {
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
                if (node.callee == "realloc") {
                  validate_expression(*node.arguments[0], pointer_type,
                                      expression_location(*node.arguments[0]));
                  count_index = 1;
                }
                validate_expression(
                    *node.arguments[count_index],
                    SemanticType{&Type::usize_type()},
                    expression_location(*node.arguments[count_index]));
                return pointer_type;
              }
              if (node.callee == "free") {
                if (!node.type_arguments.empty() || node.arguments.size() != 1)
                  throw CompileError{
                      node.location,
                      "free expects one pointer argument and no type argument"};
                const SemanticType pointer =
                    expression_type(*node.arguments.front());
                if (!pointer.is_pointer())
                  throw CompileError{node.location,
                                     "free requires a Ptr[T] argument"};
                return SemanticType{&Type::unit_type()};
              }
              const bool is_builtin_cast = builtin_type(node.callee) != nullptr;
              const bool is_reference_cast =
                  node.callee == "Ptr" || classes.contains(node.callee);
              const bool is_enum_cast = enums.contains(node.callee);
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
                return destination_type;
              }
              const auto callee_iterator = functions.find(node.callee);
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
              if (callee.is_private &&
                  callee.module_name != context_module)
                throw CompileError{
                    node.location,
                    "function '" +
                        global_key(callee.module_name, callee.name) +
                        "' is private"};
              if (node.type_arguments.size() != callee.type_parameters.size()) {
                throw CompileError{
                    node.location,
                    "function '" + node.callee + "' expects " +
                        std::to_string(callee.type_parameters.size()) +
                        " type argument(s), got " +
                        std::to_string(node.type_arguments.size())};
              }
              if ((!callee.is_variadic &&
                   node.arguments.size() != callee.parameters.size()) ||
                  (callee.is_variadic &&
                   node.arguments.size() < callee.parameters.size())) {
                throw CompileError{
                    node.location,
                    "function '" + node.callee + "' expects " +
                        (callee.is_variadic ? "at least " : "") +
                        std::to_string(callee.parameters.size()) +
                        " argument(s), got " +
                        std::to_string(node.arguments.size())};
              }

              std::unordered_map<std::string, SemanticType> substitutions;
              for (std::size_t index = 0; index < node.type_arguments.size();
                   ++index) {
                substitutions.emplace(callee.type_parameters[index],
                                      resolve_type(node.type_arguments[index],
                                                   type_parameters,
                                                   &class_arities));
              }
              const std::unordered_set<std::string> callee_parameters{
                  callee.type_parameters.begin(), callee.type_parameters.end()};
              for (const ast::TypeConstraint &constraint :
                   callee.type_constraints) {
                TraitInstance requirement =
                    resolve_trait(constraint.trait, callee_parameters);
                for (SemanticType &argument : requirement.type_arguments)
                  argument = substitute(std::move(argument), substitutions);
                const SemanticType &candidate =
                    substitutions.at(constraint.parameter);
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
                if (index >= callee.parameters.size()) {
                  const SemanticType argument_type =
                      expression_type(*node.arguments[index]);
                  if (!is_c_variadic_type(argument_type))
                    throw CompileError{
                        expression_location(*node.arguments[index]),
                        "variadic C argument has incompatible type '" +
                            argument_type.name() + "'"};
                  continue;
                }
                SemanticType expected =
                    resolve_type(callee.parameters[index].type,
                                 callee_parameters, &class_arities);
                expected = substitute(std::move(expected), substitutions);
                validate_expression(
                    *node.arguments[index], expected,
                    expression_location(*node.arguments[index]));
              }
              return substitute(resolve_type(callee.return_type,
                                             callee_parameters, &class_arities),
                                substitutions);
            } else if constexpr (std::is_same_v<Node, ast::NewExpression>) {
              const auto iterator = classes.find(node.class_name);
              if (iterator == classes.end())
                throw CompileError{node.location,
                                   "unknown class '" + node.class_name + "'"};
              const ast::ClassDeclaration &class_declaration =
                  *iterator->second;
              if (class_declaration.is_private &&
                  class_declaration.module_name != context_module)
                throw CompileError{node.location,
                                   "type '" + node.class_name +
                                       "' is private"};
              SemanticType instance_type = resolve_type(
                  ast::TypeReference{node.class_name, node.location,
                                     node.type_arguments},
                  *active_type_parameters, &class_arities);
              if (active_type_substitutions != nullptr)
                instance_type = substitute(std::move(instance_type),
                                           *active_type_substitutions);
              const auto substitutions =
                  class_substitutions(class_declaration, instance_type);
              const std::unordered_set<std::string> class_parameters{
                  class_declaration.type_parameters.begin(),
                  class_declaration.type_parameters.end()};
              for (const ast::TypeConstraint &constraint :
                   class_declaration.type_constraints) {
                TraitInstance requirement =
                    resolve_trait(constraint.trait, class_parameters);
                for (SemanticType &argument : requirement.type_arguments)
                  argument = substitute(std::move(argument), substitutions);
                const SemanticType &candidate =
                    substitutions.at(constraint.parameter);
                if (!satisfies_active_trait(candidate, requirement))
                  throw CompileError{node.location,
                                     "type '" + candidate.name() +
                                         "' does not satisfy constraint '" +
                                         requirement.declaration->name +
                                         "' for type parameter '" +
                                         constraint.parameter + "'"};
              }
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
                    field.declared_type, class_parameters, &class_arities);
                const SemanticType concrete_field_type =
                    substitute(field_type, substitutions);
                validate_expression(
                    *node.arguments[parameter_count + index],
                    concrete_field_type,
                    expression_location(
                        *node.arguments[parameter_count + index]));
                initializer_symbols.emplace(
                    field.name,
                    Symbol{concrete_field_type, field.is_mutable, true});
              }

              SymbolTable *previous_symbols = active_symbols;
              const auto *previous_type_parameters = active_type_parameters;
              const auto *previous_type_substitutions =
                  active_type_substitutions;
              active_symbols = &initializer_symbols;
              active_type_parameters = &class_parameters;
              active_type_substitutions = &substitutions;
              for (const ast::ValueDeclaration &field :
                   class_declaration.fields) {
                SemanticType field_type = resolve_type(
                    field.declared_type, class_parameters, &class_arities);
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
              return instance_type;
            } else if constexpr (std::is_same_v<Node,
                                                ast::MemberAccessExpression>) {
              const auto enum_name =
                  qualified_expression_name(*node.object);
              if (enum_name.has_value() && enums.contains(*enum_name) &&
                  (enum_name->find('.') == std::string::npos ||
                   !active_symbols->contains(
                       enum_name->substr(0, enum_name->find('.'))))) {
                const ast::EnumDeclaration &enum_declaration =
                    *enums.at(*enum_name);
                if (enum_declaration.is_private &&
                    enum_declaration.module_name != context_module)
                  throw CompileError{node.location,
                                     "type '" + *enum_name + "' is private"};
                if (!enum_declaration.type_parameters.empty())
                  throw CompileError{
                      node.location,
                      "generic enum cases require constructor syntax '" +
                          *enum_name + "." + node.member +
                          "[Types](...)'"};
                const auto enum_case =
                    std::find_if(enum_declaration.cases.begin(),
                                 enum_declaration.cases.end(),
                                 [&](const ast::EnumDeclaration::Case &item) {
                                   return item.name == node.member;
                                 });
                if (enum_case == enum_declaration.cases.end())
                  throw CompileError{node.location,
                                     "enum '" + *enum_name +
                                         "' has no case '" + node.member + "'"};
                if (!enum_case->payload_types.empty())
                  throw CompileError{node.location,
                                     "enum case '" + *enum_name +
                                         "." + node.member +
                                         "' requires constructor arguments"};
                return SemanticType{
                    nullptr, *enum_name, false, {}, false, true};
              }
              if (const auto module = qualified_expression_name(*node.object);
                  module.has_value() && global_modules.contains(*module) &&
                  !active_symbols->contains(
                      module->substr(0, module->find('.')))) {
                const ResolvedGlobal *global =
                    find_global(std::optional<std::string>{*module},
                                node.member);
                if (global == nullptr)
                  throw CompileError{node.location,
                                     "module '" + *module +
                                         "' has no global value '" +
                                         node.member + "'"};
                if (global->declaration->declaration.is_private &&
                    global->declaration->module_name != context_module)
                  throw CompileError{
                      node.location,
                      "global value '" + *module + "." + node.member +
                          "' is private"};
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
                        "field '" + node.member +
                            "' is internal to module '" +
                            class_declaration.module_name.value_or("<entry>") +
                            "'"};
                  return substitute(resolve_type(field.declared_type,
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
                        "field '" + node.member +
                            "' is internal to module '" +
                            class_declaration.module_name.value_or("<entry>") +
                            "'"};
                  return substitute(resolve_type(field.declared_type,
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
              if (const auto module =
                      qualified_expression_name(*node.object);
                  module.has_value() &&
                  !active_symbols->contains(
                      module->substr(0, module->find('.')))) {
                const std::string qualified = *module + "." + node.method;
                if (const auto function = functions.find(qualified);
                    function != functions.end()) {
                  if (function->second->is_private &&
                      function->second->module_name != context_module)
                    throw CompileError{node.location,
                                       "function '" + qualified +
                                           "' is private"};
                  return declared_call_type(
                      *function->second, node.type_arguments, node.arguments,
                      node.location, qualified);
                }
              }
              const auto enum_name =
                  qualified_expression_name(*node.object);
              if (enum_name.has_value() && enums.contains(*enum_name) &&
                  (enum_name->find('.') == std::string::npos ||
                   !active_symbols->contains(
                       enum_name->substr(0, enum_name->find('.'))))) {
                const ast::EnumDeclaration &enum_declaration =
                    *enums.at(*enum_name);
                if (enum_declaration.is_private &&
                    enum_declaration.module_name != context_module)
                  throw CompileError{node.location,
                                     "type '" + *enum_name + "' is private"};
                const SemanticType instance_type = resolve_type(
                    ast::TypeReference{*enum_name, node.location,
                                       node.type_arguments},
                    *active_type_parameters, &class_arities);
                const auto enum_case =
                    std::find_if(enum_declaration.cases.begin(),
                                 enum_declaration.cases.end(),
                                 [&](const ast::EnumDeclaration::Case &item) {
                                   return item.name == node.method;
                                 });
                if (enum_case == enum_declaration.cases.end())
                  throw CompileError{node.location,
                                     "enum '" + *enum_name +
                                         "' has no case '" + node.method + "'"};
                if (node.arguments.size() != enum_case->payload_types.size())
                  throw CompileError{
                      node.location,
                      "enum case '" + *enum_name + "." +
                          node.method + "' expects " +
                          std::to_string(enum_case->payload_types.size()) +
                          " argument(s), got " +
                          std::to_string(node.arguments.size())};
                std::unordered_map<std::string, SemanticType> substitutions;
                for (std::size_t index = 0;
                     index < enum_declaration.type_parameters.size(); ++index)
                  substitutions.emplace(enum_declaration.type_parameters[index],
                                        instance_type.type_arguments[index]);
                const std::unordered_set<std::string> enum_parameters{
                    enum_declaration.type_parameters.begin(),
                    enum_declaration.type_parameters.end()};
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
                if (node.method == "store") {
                  if (node.arguments.size() != 2)
                    throw CompileError{
                        node.location,
                        "Ptr.store expects an index and a value"};
                  validate_expression(*node.arguments[0],
                                      SemanticType{&Type::usize_type()},
                                      expression_location(*node.arguments[0]));
                  validate_expression(*node.arguments[1],
                                      object_type.type_arguments.front(),
                                      expression_location(*node.arguments[1]));
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
              if (node.type_arguments.size() != method->type_parameters.size())
                throw CompileError{
                    node.location,
                    "method '" + node.method + "' expects " +
                        std::to_string(method->type_parameters.size()) +
                        " type argument(s), got " +
                        std::to_string(node.type_arguments.size())};
              for (std::size_t index = 0;
                   index < method->type_parameters.size(); ++index) {
                method_parameters.insert(method->type_parameters[index]);
                SemanticType argument =
                    resolve_type(node.type_arguments[index],
                                 *active_type_parameters, &class_arities);
                if (active_type_substitutions != nullptr)
                  argument = substitute(std::move(argument),
                                        *active_type_substitutions);
                substitutions.emplace(method->type_parameters[index],
                                      std::move(argument));
              }
              for (const ast::TypeConstraint &constraint :
                   method->type_constraints) {
                TraitInstance requirement =
                    resolve_trait(constraint.trait, method_parameters);
                for (SemanticType &argument : requirement.type_arguments)
                  argument = substitute(std::move(argument), substitutions);
                const SemanticType &candidate =
                    substitutions.at(constraint.parameter);
                if (!satisfies_active_trait(candidate, requirement))
                  throw CompileError{node.location,
                                     "type '" + candidate.name() +
                                         "' does not satisfy constraint '" +
                                         requirement.declaration->name +
                                         "' for type parameter '" +
                                         constraint.parameter + "'"};
              }
              if (node.arguments.size() != method->parameters.size())
                throw CompileError{
                    node.location,
                    "method '" + node.method + "' expects " +
                        std::to_string(method->parameters.size()) +
                        " argument(s), got " +
                        std::to_string(node.arguments.size())};
              for (std::size_t index = 0; index < node.arguments.size();
                   ++index) {
                const SemanticType expected =
                    resolve_type(method->parameters[index].type,
                                 method_parameters, &class_arities);
                validate_expression(
                    *node.arguments[index], substitute(expected, substitutions),
                    expression_location(*node.arguments[index]));
              }
              if (method->is_consuming) {
                if (const auto *identifier =
                        std::get_if<ast::IdentifierExpression>(
                            &node.object->value)) {
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
                    throw CompileError{
                        node.location,
                        "owning global value '" + identifier->name +
                            "' cannot be consumed"};
                  active_symbols->at(identifier->name).is_initialized = false;
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
              const auto previous_transfer_protected =
                  transfer_protected_values;
              for (const auto &[name, symbol] : *active_symbols) {
                static_cast<void>(symbol);
                transfer_protected_values.insert(name);
              }
              if (!scrutinee_type.is_enum())
                throw CompileError{node.location,
                                   "match requires an enum value, got '" +
                                       scrutinee_type.name() + "'"};
              if (node.arms.empty())
                throw CompileError{node.location,
                                   "match requires at least one case"};

              const ast::EnumDeclaration &enum_declaration =
                  *enums.at(scrutinee_type.parameter);
              std::unordered_map<std::string, SemanticType> substitutions;
              for (std::size_t index = 0;
                   index < enum_declaration.type_parameters.size(); ++index) {
                substitutions.emplace(enum_declaration.type_parameters[index],
                                      scrutinee_type.type_arguments[index]);
              }
              const std::unordered_set<std::string> enum_parameters{
                  enum_declaration.type_parameters.begin(),
                  enum_declaration.type_parameters.end()};
              std::unordered_set<std::string> matched_cases;
              std::optional<SemanticType> result_type;
              for (const ast::MatchExpression::Arm &arm : node.arms) {
                const auto enum_case = std::find_if(
                    enum_declaration.cases.begin(),
                    enum_declaration.cases.end(),
                    [&](const ast::EnumDeclaration::Case &candidate) {
                      return candidate.name == arm.case_name;
                    });
                if (enum_case == enum_declaration.cases.end())
                  throw CompileError{arm.location, "enum '" +
                                                       enum_declaration.name +
                                                       "' has no case '" +
                                                       arm.case_name + "'"};
                if (!matched_cases.insert(arm.case_name).second)
                  throw CompileError{arm.location, "match case '" +
                                                       arm.case_name +
                                                       "' is already handled"};
                if (arm.bindings.size() != enum_case->payload_types.size())
                  throw CompileError{
                      arm.location,
                      "enum case '" + arm.case_name + "' contains " +
                          std::to_string(enum_case->payload_types.size()) +
                          " value(s), but the pattern binds " +
                          std::to_string(arm.bindings.size())};

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
                for (const std::string &binding : arm.bindings)
                  transfer_protected_values.erase(binding);
                const SemanticType arm_type = expression_type(*arm.expression);
                transfer_protected_values = arm_transfer_protected;
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
              for (const ast::EnumDeclaration::Case &enum_case :
                   enum_declaration.cases) {
                if (matched_cases.contains(enum_case.name))
                  continue;
                if (!missing_cases.empty())
                  missing_cases += ", ";
                missing_cases += enum_case.name;
              }
              if (!missing_cases.empty())
                throw CompileError{node.location,
                                   "non-exhaustive match for enum '" +
                                       enum_declaration.name +
                                       "': missing case(s): " + missing_cases};
              transfer_protected_values = previous_transfer_protected;
              return *result_type;
            } else if constexpr (std::is_same_v<Node, ast::MoveExpression>) {
              const auto *identifier =
                  std::get_if<ast::IdentifierExpression>(&node.operand->value);
              if (identifier == nullptr)
                throw CompileError{node.location,
                                   "move requires a local value identifier"};
              if (transfer_protected_values.contains(identifier->name))
                throw CompileError{
                    node.location,
                    "owning value '" + identifier->name +
                        "' cannot be moved from a loop, branch expression, "
                        "or closure"};
              if (deferred_values.contains(identifier->name))
                throw CompileError{
                    node.location,
                    "owning value '" + identifier->name +
                        "' is scheduled for deferred cleanup"};
              if (!active_symbols->contains(identifier->name) &&
                  visible_global(identifier->name) != nullptr)
                throw CompileError{
                    node.location,
                    "owning global value '" + identifier->name +
                        "' cannot be moved"};
              const SemanticType moved_type = expression_type(*node.operand);
              if (moved_type.is_class() &&
                  classes.at(moved_type.parameter)->is_value_type)
                throw CompileError{node.location,
                                   "struct values are copied and cannot be moved"};
              if (moved_type.is_concrete() || moved_type.is_enum())
                throw CompileError{
                    node.location,
                    "move requires an owning class, function, pointer, or "
                    "generic value"};
              active_symbols->at(identifier->name).is_initialized = false;
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
              const SemanticType right_type = expression_type(*node.right);
              if (!same_type(left_type, right_type)) {
                throw CompileError{
                    node.location,
                    "binary operator operands must have the same type, got '" +
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
                if (!is_concrete ||
                    !left_type.concrete->is_integer()) {
                  throw CompileError{
                      node.location,
                      "operator '%' requires integer operands"};
                }
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
                    !left_type.is_enum()) {
                  throw CompileError{
                      node.location,
                      "equality operators require primitive operands"};
                }
                if (left_type.is_enum()) {
                  const ast::EnumDeclaration &declaration =
                      *enums.at(left_type.parameter);
                  if (std::any_of(
                          declaration.cases.begin(), declaration.cases.end(),
                          [](const ast::EnumDeclaration::Case &enum_case) {
                            return !enum_case.payload_types.empty();
                          }))
                    throw CompileError{
                        node.location,
                        "enums with payloads must be inspected with match"};
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
              throw CompileError{node.location,
                                 "unsupported binary operator"};
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

    std::function<bool(const std::vector<ast::Statement> &, SymbolTable &)>
        validate_block;
    validate_block = [&](const std::vector<ast::Statement> &statements,
                         SymbolTable &block_symbols) {
      SymbolTable *previous_symbols = active_symbols;
      const auto previous_deferred_values = deferred_values;
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
            symbol.is_initialized = then_symbols.at(name).is_initialized &&
                                    else_symbols.at(name).is_initialized;
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
                throw CompileError{
                    (*loop)->location,
                    "owning value '" + identifier->name +
                        "' is scheduled for deferred cleanup"};
              block_symbols.at(identifier->name).is_initialized = false;
            }
          continue;
        }

        if (const auto *declaration =
                std::get_if<ast::ValueDeclaration>(&statement)) {
          const SemanticType declared_type = resolve_type(
              declaration->declared_type, type_parameters, &class_arities);
          if (declared_type.is_concrete() &&
              declared_type.concrete->kind() == TypeKind::Unit)
            throw CompileError{declaration->location,
                               "Unit cannot be used as a value type"};
          if (block_symbols.contains(declaration->name))
            throw CompileError{declaration->location,
                               "value '" + declaration->name +
                                   "' is already declared"};
          if (declaration->initializer.has_value())
            validate_expression(*declaration->initializer, declared_type,
                                declaration->location);
          block_symbols.emplace(declaration->name,
                                Symbol{declared_type, declaration->is_mutable,
                                       declaration->initializer.has_value()});
          continue;
        }

        if (const auto *assignment =
                std::get_if<ast::AssignmentStatement>(&statement)) {
          if (!assignment->object.empty()) {
            if (!block_symbols.contains(assignment->object) &&
                global_modules.contains(assignment->object)) {
              const ResolvedGlobal *global = find_global(
                  std::optional<std::string>{assignment->object},
                  assignment->name);
              if (global == nullptr)
                throw CompileError{
                    assignment->location,
                    "module '" + assignment->object +
                        "' has no global value '" + assignment->name + "'"};
              if (global->declaration->declaration.is_private &&
                  global->declaration->module_name != context_module)
                throw CompileError{
                    assignment->location,
                    "global value '" + assignment->object + "." +
                        assignment->name + "' is private"};
              if (!global->symbol.is_mutable)
                throw CompileError{
                    assignment->location,
                    "cannot assign to immutable global value '" +
                        assignment->object + "." + assignment->name + "'"};
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
                  "field '" + assignment->name +
                      "' is internal to module '" +
                      class_declaration.module_name.value_or("<entry>") + "'"};
            if (!matched->is_mutable)
              throw CompileError{assignment->location,
                                 "cannot assign to immutable field '" +
                                     assignment->name + "'"};
            validate_expression(
                assignment->expression,
                substitute(resolve_type(matched->declared_type,
                                        class_parameters, &class_arities),
                           substitutions),
                assignment->location);
            continue;
          }
          const auto iterator = block_symbols.find(assignment->name);
          if (iterator == block_symbols.end()) {
            const Symbol *global = visible_global(assignment->name);
            if (global == nullptr)
              throw CompileError{assignment->location,
                                 "unknown value '" + assignment->name + "'"};
            if (!global->is_mutable)
              throw CompileError{assignment->location,
                                 "cannot assign to immutable global value '" +
                                     assignment->name + "'"};
            validate_expression(assignment->expression, global->type,
                                assignment->location);
            continue;
          }
          if (deferred_values.contains(assignment->name))
            throw CompileError{
                assignment->location,
                "owning value '" + assignment->name +
                    "' is scheduled for deferred cleanup"};
          if (!iterator->second.is_mutable)
            throw CompileError{assignment->location,
                               "cannot assign to immutable value '" +
                                   assignment->name + "'"};
          validate_expression(assignment->expression, iterator->second.type,
                              assignment->location);
          iterator->second.is_initialized = true;
          continue;
        }

        if (const auto *deletion =
                std::get_if<ast::DeleteStatement>(&statement)) {
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value);
              identifier != nullptr &&
              deferred_values.contains(identifier->name))
            throw CompileError{
                deletion->location,
                "owning value '" + identifier->name +
                    "' is scheduled for deferred cleanup"};
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value);
              identifier != nullptr && !block_symbols.contains(identifier->name) &&
              visible_global(identifier->name) != nullptr)
            throw CompileError{
                deletion->location,
                "owning global value '" + identifier->name +
                    "' is destroyed automatically"};
          const SemanticType deleted_type =
              expression_type(deletion->expression);
          if (deleted_type.is_class() &&
              classes.at(deleted_type.parameter)->is_value_type)
            throw CompileError{deletion->location,
                               "struct values do not require delete"};
          if (!deleted_type.is_class() && !deleted_type.is_function())
            throw CompileError{deletion->location,
                               "delete requires an object or a function value"};
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value))
            block_symbols.at(identifier->name).is_initialized = false;
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
              throw CompileError{
                  deletion->location,
                  "owning global value '" + identifier->name +
                      "' is destroyed automatically"};
            if (deferred_values.contains(identifier->name))
              throw CompileError{
                  deletion->location,
                  "owning value '" + identifier->name +
                      "' is already scheduled for deferred cleanup"};
            const SemanticType deleted_type =
                expression_type(deletion->expression);
            if (deleted_type.is_class() &&
                classes.at(deleted_type.parameter)->is_value_type)
              throw CompileError{deletion->location,
                                 "struct values do not require delete"};
            if (!deleted_type.is_class() && !deleted_type.is_function())
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
          static_cast<void>(expression_type(expression_statement->expression));
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
          if (const auto *identifier =
                  std::get_if<ast::IdentifierExpression>(
                      &return_statement.expression->value);
              identifier != nullptr &&
              deferred_values.contains(identifier->name))
            throw CompileError{
                return_statement.location,
                "owning value '" + identifier->name +
                    "' is scheduled for deferred cleanup"};
          validate_return_expression(return_statement);
        }
        has_terminator = true;
      }
      active_symbols = previous_symbols;
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
