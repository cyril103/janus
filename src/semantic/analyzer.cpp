#include "janus/semantic/analyzer.hpp"

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

const janus::Type *builtin_type(std::string_view name) {
  if (name == "int")
    return &janus::Type::int_type();
  if (name == "double")
    return &janus::Type::double_type();
  if (name == "byte")
    return &janus::Type::byte_type();
  if (name == "char")
    return &janus::Type::char_type();
  if (name == "bool")
    return &janus::Type::bool_type();
  if (name == "string")
    return &janus::Type::string_type();
  if (name == "Unit")
    return &janus::Type::unit_type();
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
  case janus::TypeKind::Double:
  case janus::TypeKind::Byte:
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
  case janus::TypeKind::Byte:
  case janus::TypeKind::Char:
  case janus::TypeKind::Bool:
  case janus::TypeKind::USize:
    return true;
  default:
    return false;
  }
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

std::optional<std::int64_t>
integer_literal_value(const janus::ast::Expression &expression) {
  if (const auto *literal = std::get_if<janus::ast::IntegerLiteralExpression>(
          &expression.value)) {
    return literal->value;
  }
  if (const auto *unary =
          std::get_if<janus::ast::UnaryExpression>(&expression.value);
      unary != nullptr &&
      unary->operation == janus::ast::UnaryOperator::Negate) {
    if (const auto value = integer_literal_value(*unary->operand)) {
      return -*value;
    }
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

AnalysisResult Analyzer::analyze(const ast::Program &program) const {
  AnalysisResult result;
  std::unordered_map<std::string, const ast::FunctionDeclaration *> functions;
  std::unordered_map<std::string, const ast::ClassDeclaration *> classes;
  std::unordered_map<std::string, const ast::EnumDeclaration *> enums;
  std::unordered_map<std::string, const ast::TraitDeclaration *> traits;
  std::unordered_map<std::string, std::size_t> class_arities;

  for (const ast::EnumDeclaration &enum_declaration : program.enums) {
    if (builtin_type(enum_declaration.name) != nullptr ||
        enum_declaration.name == "Function")
      throw CompileError{enum_declaration.location,
                         "enum '" + enum_declaration.name +
                             "' conflicts with a built-in type"};
    if (!enums.emplace(enum_declaration.name, &enum_declaration).second)
      throw CompileError{enum_declaration.location,
                         "enum '" + enum_declaration.name +
                             "' is already declared"};
    class_arities.emplace(enum_declaration.name,
                          enum_arity_marker +
                              enum_declaration.type_parameters.size());
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
    if (enums.contains(trait_declaration.name))
      throw CompileError{trait_declaration.location,
                         "trait '" + trait_declaration.name +
                             "' conflicts with an enum"};
    if (!traits.emplace(trait_declaration.name, &trait_declaration).second)
      throw CompileError{trait_declaration.location,
                         "trait '" + trait_declaration.name +
                             "' is already declared"};
  }
  for (const ast::ClassDeclaration &class_declaration : program.classes) {
    if (builtin_type(class_declaration.name) != nullptr ||
        class_declaration.name == "Function")
      throw CompileError{class_declaration.location,
                         "class '" + class_declaration.name +
                             "' conflicts with a built-in type"};
    if (enums.contains(class_declaration.name))
      throw CompileError{class_declaration.location,
                         "class '" + class_declaration.name +
                             "' conflicts with an enum"};
    if (traits.contains(class_declaration.name))
      throw CompileError{class_declaration.location,
                         "class '" + class_declaration.name +
                             "' conflicts with a trait"};
    if (!classes.emplace(class_declaration.name, &class_declaration).second) {
      throw CompileError{class_declaration.location,
                         "class '" + class_declaration.name +
                             "' is already declared"};
    }
    class_arities.emplace(class_declaration.name,
                          class_declaration.type_parameters.size());
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
        std::unordered_set<std::string> constrained;
        for (const ast::TypeConstraint &constraint : constraints) {
          if (!type_parameters.contains(constraint.parameter))
            throw CompileError{constraint.location,
                               "constraint targets unknown type parameter '" +
                                   constraint.parameter + "'"};
          if (!constrained.insert(constraint.parameter).second)
            throw CompileError{constraint.location,
                               "type parameter '" + constraint.parameter +
                                   "' already has a trait constraint"};
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
  };
  std::vector<FunctionContext> contexts;
  for (const ast::FunctionDeclaration &function : program.functions) {
    contexts.push_back(FunctionContext{&function, nullptr, nullptr});
    if (!functions.emplace(function.name, &function).second) {
      throw CompileError{function.location, "function '" + function.name +
                                                "' is already declared"};
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
      contexts.push_back(FunctionContext{&method, &class_declaration, nullptr});
    }
    if (class_declaration.destructor.has_value())
      contexts.push_back(FunctionContext{nullptr, &class_declaration,
                                         &*class_declaration.destructor});
  }

  const auto main_iterator = functions.find("main");
  if (main_iterator == functions.end()) {
    throw CompileError{SourceLocation{},
                       "program must declare an entry point 'main'"};
  }

  for (const FunctionContext &context : contexts) {
    const bool is_destructor = context.destructor != nullptr;
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
    if (!is_destructor)
      validate_constraints(context.function->type_constraints, type_parameters);

    const SemanticType return_type =
        is_destructor ? SemanticType{&Type::unit_type()}
                      : resolve_type(context.function->return_type,
                                     type_parameters, &class_arities);
    if (!is_destructor && owner == nullptr && function_name == "main") {
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
    std::unordered_map<std::string, TraitInstance> active_trait_constraints;
    const auto add_active_constraints =
        [&](const std::vector<ast::TypeConstraint> &constraints) {
          for (const ast::TypeConstraint &constraint : constraints)
            active_trait_constraints.emplace(
                constraint.parameter,
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
          iterator == active_trait_constraints.end() ||
          iterator->second.declaration != requirement.declaration ||
          iterator->second.type_arguments.size() !=
              requirement.type_arguments.size())
        return false;
      return std::equal(iterator->second.type_arguments.begin(),
                        iterator->second.type_arguments.end(),
                        requirement.type_arguments.begin(), same_type);
    };
    SymbolTable *active_symbols = &symbols;
    const std::unordered_set<std::string> *active_type_parameters =
        &type_parameters;
    const std::unordered_map<std::string, SemanticType>
        *active_type_substitutions = nullptr;
    bool inside_lambda = false;

    std::function<SemanticType(const ast::Expression &)> expression_type;
    std::function<void(const ast::Expression &, const SemanticType &,
                       SourceLocation)>
        validate_expression;
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
      const SemanticType actual = expression_type(expression);
      if (same_type(actual, expected))
        return;

      if (expected.is_concrete() &&
          expected.concrete->kind() == TypeKind::Byte) {
        if (const auto literal = integer_literal_value(expression)) {
          if (*literal >= std::numeric_limits<std::int8_t>::min() &&
              *literal <= std::numeric_limits<std::int8_t>::max()) {
            return;
          }
          throw CompileError{
              expression_location(expression),
              "integer literal is outside the signed 8-bit range"};
        }
      }

      throw CompileError{location, "cannot use expression of type '" +
                                       actual.name() + "' where type '" +
                                       expected.name() + "' is required"};
    };

    expression_type = [&](const ast::Expression &expression) -> SemanticType {
      return std::visit(
          [&](const auto &node) -> SemanticType {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, ast::IntegerLiteralExpression>) {
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
              inside_lambda = true;
              signature.push_back(expression_type(*node.body));
              inside_lambda = previous_inside_lambda;
              active_symbols = previous_symbols;
              return SemanticType{
                  nullptr, "Function", false, std::move(signature),
                  false,   false,      true};
            } else if constexpr (std::is_same_v<Node, ast::CallExpression>) {
              if (const auto local = active_symbols->find(node.callee);
                  local != active_symbols->end()) {
                if (!local->second.is_initialized)
                  throw CompileError{node.location,
                                     "function value '" + node.callee +
                                         "' is used before initialization"};
                if (!local->second.type.is_function())
                  throw CompileError{node.location, "value '" + node.callee +
                                                        "' is not callable"};
                if (!node.type_arguments.empty())
                  throw CompileError{
                      node.location,
                      "a function value does not accept type arguments"};
                const std::vector<SemanticType> &signature =
                    local->second.type.type_arguments;
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
                     argument.concrete->kind() != TypeKind::Double &&
                     argument.concrete->kind() != TypeKind::Byte &&
                     argument.concrete->kind() != TypeKind::Char &&
                     argument.concrete->kind() != TypeKind::Bool &&
                     argument.concrete->kind() != TypeKind::String &&
                     argument.concrete->kind() != TypeKind::USize))
                  throw CompileError{
                      node.location,
                      node.callee +
                          " supports int, double, byte, char, bool, string, "
                          "and usize values"};
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
                throw CompileError{node.location,
                                   "unknown function '" + node.callee + "'"};
              }
              const ast::FunctionDeclaration &callee = *callee_iterator->second;
              if (node.type_arguments.size() != callee.type_parameters.size()) {
                throw CompileError{
                    node.location,
                    "function '" + node.callee + "' expects " +
                        std::to_string(callee.type_parameters.size()) +
                        " type argument(s), got " +
                        std::to_string(node.type_arguments.size())};
              }
              if (node.arguments.size() != callee.parameters.size()) {
                throw CompileError{
                    node.location,
                    "function '" + node.callee + "' expects " +
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
              if (const auto *identifier =
                      std::get_if<ast::IdentifierExpression>(
                          &node.object->value);
                  identifier != nullptr && enums.contains(identifier->name)) {
                const ast::EnumDeclaration &enum_declaration =
                    *enums.at(identifier->name);
                if (!enum_declaration.type_parameters.empty())
                  throw CompileError{
                      node.location,
                      "generic enum cases require constructor syntax '" +
                          enum_declaration.name + "." + node.member +
                          "[Types](...)'"};
                const auto enum_case =
                    std::find_if(enum_declaration.cases.begin(),
                                 enum_declaration.cases.end(),
                                 [&](const ast::EnumDeclaration::Case &item) {
                                   return item.name == node.member;
                                 });
                if (enum_case == enum_declaration.cases.end())
                  throw CompileError{node.location,
                                     "enum '" + enum_declaration.name +
                                         "' has no case '" + node.member + "'"};
                if (!enum_case->payload_types.empty())
                  throw CompileError{node.location,
                                     "enum case '" + enum_declaration.name +
                                         "." + node.member +
                                         "' requires constructor arguments"};
                return SemanticType{
                    nullptr, enum_declaration.name, false, {}, false, true};
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
              if (const auto *identifier =
                      std::get_if<ast::IdentifierExpression>(
                          &node.object->value);
                  identifier != nullptr && enums.contains(identifier->name)) {
                const ast::EnumDeclaration &enum_declaration =
                    *enums.at(identifier->name);
                const SemanticType instance_type = resolve_type(
                    ast::TypeReference{enum_declaration.name, node.location,
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
                                     "enum '" + enum_declaration.name +
                                         "' has no case '" + node.method + "'"};
                if (node.arguments.size() != enum_case->payload_types.size())
                  throw CompileError{
                      node.location,
                      "enum case '" + enum_declaration.name + "." +
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
                trait_declaration = constraint->second.declaration;
                method_parameters.insert(
                    trait_declaration->type_parameters.begin(),
                    trait_declaration->type_parameters.end());
                for (std::size_t index = 0;
                     index < trait_declaration->type_parameters.size(); ++index)
                  substitutions.emplace(
                      trait_declaration->type_parameters[index],
                      constraint->second.type_arguments[index]);
                for (const ast::FunctionDeclaration &candidate :
                     trait_declaration->methods) {
                  if (candidate.name == node.method)
                    method = &candidate;
                }
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
                  if (owner_field_names.contains(identifier->name))
                    throw CompileError{node.location,
                                       "consuming field '" + identifier->name +
                                           "' requires an explicit move"};
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
              const SemanticType then_type =
                  expression_type(*node.then_expression);
              const SemanticType else_type =
                  expression_type(*node.else_expression);
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
                const SemanticType arm_type = expression_type(*arm.expression);
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
              return *result_type;
            } else if constexpr (std::is_same_v<Node, ast::MoveExpression>) {
              const auto *identifier =
                  std::get_if<ast::IdentifierExpression>(&node.operand->value);
              if (identifier == nullptr)
                throw CompileError{node.location,
                                   "move requires a local value identifier"};
              const SemanticType moved_type = expression_type(*node.operand);
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
                  (operand_type.concrete->kind() != TypeKind::Int &&
                   operand_type.concrete->kind() != TypeKind::Byte &&
                   operand_type.concrete->kind() != TypeKind::Double)) {
                throw CompileError{
                    node.location,
                    "unary operator '-' requires an operand of type 'int', "
                    "'byte', or 'double'"};
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
                  is_concrete &&
                  (kind == TypeKind::Int || kind == TypeKind::Byte ||
                   kind == TypeKind::Double || kind == TypeKind::USize);
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
                    (kind != TypeKind::Int && kind != TypeKind::Byte &&
                     kind != TypeKind::USize)) {
                  throw CompileError{
                      node.location,
                      "operator '%' requires operands of type 'int', 'byte', "
                      "or 'usize'"};
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
      active_symbols = &block_symbols;
      bool has_return = false;
      for (const ast::Statement &statement : statements) {
        if (has_return)
          throw CompileError{statement_location(statement),
                             "statement after return is unreachable"};

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
          has_return = then_returns && else_returns;
          continue;
        }

        if (const auto *loop =
                std::get_if<std::shared_ptr<ast::WhileStatement>>(&statement)) {
          validate_expression((*loop)->condition,
                              SemanticType{&Type::bool_type()},
                              (*loop)->location);
          SymbolTable loop_symbols = block_symbols;
          static_cast<void>(validate_block((*loop)->body, loop_symbols));
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
                     constraint != active_trait_constraints.end() &&
                     constraint->second.declaration->name == "Iterable" &&
                     constraint->second.type_arguments.size() == 1) {
            element_type = constraint->second.type_arguments.front();
          }
          if (!element_type.has_value())
            throw CompileError{(*loop)->location,
                               "for requires an Iterator[T] or Iterable[T], "
                               "got '" +
                                   source_type.name() + "'"};
          SymbolTable loop_symbols = block_symbols;
          loop_symbols.insert_or_assign((*loop)->binding,
                                        Symbol{*element_type, false, true});
          static_cast<void>(validate_block((*loop)->body, loop_symbols));
          active_symbols = &block_symbols;
          if (consumes_source)
            if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                    &(*loop)->iterator.value))
              block_symbols.at(identifier->name).is_initialized = false;
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
          if (iterator == block_symbols.end())
            throw CompileError{assignment->location,
                               "unknown value '" + assignment->name + "'"};
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
          const SemanticType deleted_type =
              expression_type(deletion->expression);
          if (!deleted_type.is_class() && !deleted_type.is_function())
            throw CompileError{deletion->location,
                               "delete requires an object or a function value"};
          if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                  &deletion->expression.value))
            block_symbols.at(identifier->name).is_initialized = false;
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
          validate_expression(*return_statement.expression, return_type,
                              return_statement.location);
        }
        has_return = true;
      }
      active_symbols = previous_symbols;
      return has_return;
    };

    const bool has_return = validate_block(body, symbols);

    if (!has_return && (!return_type.is_concrete() ||
                        return_type.concrete->kind() != TypeKind::Unit)) {
      throw CompileError{function_location, "function '" + function_name +
                                                "' must return a value"};
    }
    const std::string analysis_name =
        owner == nullptr ? function_name : owner->name + "." + function_name;
    result.functions.emplace(analysis_name, std::move(symbols));
  }

  return result;
}

} // namespace janus::semantic
