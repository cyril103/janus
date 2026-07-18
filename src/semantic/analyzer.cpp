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
  if (left.is_pointer() || right.is_pointer())
    return left.is_pointer() && right.is_pointer() &&
           left.type_arguments.size() == 1 &&
           right.type_arguments.size() == 1 &&
           same_type(left.type_arguments.front(), right.type_arguments.front());
  return left.is_concrete() ? left.concrete->kind() == right.concrete->kind()
                            : left.parameter == right.parameter;
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
  std::unordered_map<std::string, std::size_t> class_arities;

  for (const ast::ClassDeclaration &class_declaration : program.classes) {
    if (!classes.emplace(class_declaration.name, &class_declaration).second) {
      throw CompileError{class_declaration.location,
                         "class '" + class_declaration.name +
                             "' is already declared"};
    }
    class_arities.emplace(class_declaration.name,
                          class_declaration.type_parameters.size());
  }
  for (const ast::ClassDeclaration &class_declaration : program.classes) {
    std::unordered_set<std::string> parameters;
    for (const std::string &parameter : class_declaration.type_parameters) {
      if (!parameters.insert(parameter).second)
        throw CompileError{class_declaration.location,
                           "type parameter '" + parameter +
                               "' is already declared"};
      if (builtin_type(parameter) != nullptr)
        throw CompileError{class_declaration.location,
                           "type parameter '" + parameter +
                               "' conflicts with a built-in type"};
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
      if (builtin_type(parameter) != nullptr) {
        throw CompileError{function_location,
                           "type parameter '" + parameter +
                               "' conflicts with a built-in type"};
      }
    }

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
    if (owner != nullptr) {
      std::vector<SemanticType> owner_arguments;
      for (const std::string &parameter : owner->type_parameters)
        owner_arguments.push_back(SemanticType{nullptr, parameter});
      symbols.emplace("this", Symbol{SemanticType{nullptr, owner->name, true,
                                                  std::move(owner_arguments)},
                                     false, true});
      for (const ast::ValueDeclaration &field : owner->constructor_fields) {
        symbols.emplace(field.name,
                        Symbol{resolve_type(field.declared_type,
                                            type_parameters, &class_arities),
                               field.is_mutable, true});
      }
      for (const ast::ValueDeclaration &field : owner->fields) {
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
    SymbolTable *active_symbols = &symbols;
    const std::unordered_set<std::string> *active_type_parameters =
        &type_parameters;
    const std::unordered_map<std::string, SemanticType>
        *active_type_substitutions = nullptr;

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
            } else if constexpr (std::is_same_v<Node, ast::CallExpression>) {
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
              const Type *conversion_type = builtin_type(node.callee);
              const bool is_integer_conversion =
                  conversion_type != nullptr &&
                  (conversion_type->kind() == TypeKind::Int ||
                   conversion_type->kind() == TypeKind::Byte ||
                   conversion_type->kind() == TypeKind::USize);
              if (is_integer_conversion) {
                if (!node.type_arguments.empty() || node.arguments.size() != 1)
                  throw CompileError{node.location,
                                     "integer conversion '" + node.callee +
                                         "' expects exactly one argument"};
                const SemanticType source_type =
                    expression_type(*node.arguments.front());
                if (!source_type.is_concrete() ||
                    (source_type.concrete->kind() != TypeKind::Int &&
                     source_type.concrete->kind() != TypeKind::Byte &&
                     source_type.concrete->kind() != TypeKind::USize))
                  throw CompileError{
                      node.location,
                      "integer conversion '" + node.callee +
                          "' requires an int, byte, or usize argument"};
                return SemanticType{conversion_type};
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
              const SemanticType instance_type = resolve_type(
                  ast::TypeReference{node.class_name, node.location,
                                     node.type_arguments},
                  type_parameters, &class_arities);
              const auto substitutions =
                  class_substitutions(class_declaration, instance_type);
              const std::unordered_set<std::string> class_parameters{
                  class_declaration.type_parameters.begin(),
                  class_declaration.type_parameters.end()};
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
              const SemanticType object_type = expression_type(*node.object);
              if (object_type.is_pointer()) {
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
              if (!object_type.is_class())
                throw CompileError{node.location,
                                   "method call requires an object"};
              const ast::ClassDeclaration &class_declaration =
                  *classes.at(object_type.parameter);
              const auto substitutions =
                  class_substitutions(class_declaration, object_type);
              const std::unordered_set<std::string> class_parameters{
                  class_declaration.type_parameters.begin(),
                  class_declaration.type_parameters.end()};
              const ast::FunctionDeclaration *method = nullptr;
              for (const ast::FunctionDeclaration &candidate :
                   class_declaration.methods) {
                if (candidate.name == node.method)
                  method = &candidate;
              }
              if (method == nullptr)
                throw CompileError{node.location,
                                   "class '" + class_declaration.name +
                                       "' has no method '" + node.method + "'"};
              if (method->is_private &&
                  (owner == nullptr || owner->name != class_declaration.name))
                throw CompileError{node.location,
                                   "method '" + node.method +
                                       "' is private in class '" +
                                       class_declaration.name + "'"};
              if (!method->type_parameters.empty())
                throw CompileError{node.location,
                                   "generic methods are not yet supported"};
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
                                 class_parameters, &class_arities);
                validate_expression(
                    *node.arguments[index], substitute(expected, substitutions),
                    expression_location(*node.arguments[index]));
              }
              return substitute(resolve_type(method->return_type,
                                             class_parameters, &class_arities),
                                substitutions);
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
                if (!is_concrete && !left_type.is_pointer()) {
                  throw CompileError{
                      node.location,
                      "equality operators require primitive operands"};
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
                          std::is_same_v<Node,
                                         std::shared_ptr<ast::WhileStatement>>)
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
          if (!deleted_type.is_class())
            throw CompileError{deletion->location, "delete requires an object"};
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
