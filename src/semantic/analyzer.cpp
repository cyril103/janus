#include "janus/semantic/analyzer.hpp"

#include "janus/diagnostics/compile_error.hpp"

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
  return nullptr;
}

janus::semantic::SemanticType
resolve_type(const janus::ast::TypeReference &reference,
             const std::unordered_set<std::string> &type_parameters,
             const std::unordered_set<std::string> *class_names = nullptr) {
  if (const janus::Type *type = builtin_type(reference.name)) {
    return janus::semantic::SemanticType{type, {}};
  }
  if (type_parameters.contains(reference.name)) {
    return janus::semantic::SemanticType{nullptr, reference.name};
  }
  if (class_names != nullptr && class_names->contains(reference.name)) {
    return janus::semantic::SemanticType{nullptr, reference.name, true};
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
           left.parameter == right.parameter;
  return left.is_concrete() ? left.concrete->kind() == right.concrete->kind()
                            : left.parameter == right.parameter;
}

janus::semantic::SemanticType
substitute(janus::semantic::SemanticType type,
           const std::unordered_map<std::string, janus::semantic::SemanticType>
               &substitutions) {
  if (!type.is_concrete()) {
    if (const auto iterator = substitutions.find(type.parameter);
        iterator != substitutions.end()) {
      return iterator->second;
    }
  }
  return type;
}

janus::SourceLocation expression_location(const janus::ast::Expression &expr) {
  return std::visit([](const auto &node) { return node.location; }, expr.value);
}

} // namespace

namespace janus::semantic {

AnalysisResult Analyzer::analyze(const ast::Program &program) const {
  AnalysisResult result;
  std::unordered_map<std::string, const ast::FunctionDeclaration *> functions;
  std::unordered_map<std::string, const ast::ClassDeclaration *> classes;
  std::unordered_set<std::string> class_names;

  for (const ast::ClassDeclaration &class_declaration : program.classes) {
    if (!classes.emplace(class_declaration.name, &class_declaration).second) {
      throw CompileError{class_declaration.location,
                         "class '" + class_declaration.name +
                             "' is already declared"};
    }
    class_names.insert(class_declaration.name);
    if (class_declaration.destructor.has_value() &&
        !class_declaration.destructor->body.empty()) {
      throw CompileError{class_declaration.destructor->location,
                         "non-empty destructor bodies are not yet supported"};
    }
  }

  for (const ast::FunctionDeclaration &function : program.functions) {
    if (!functions.emplace(function.name, &function).second) {
      throw CompileError{function.location, "function '" + function.name +
                                                "' is already declared"};
    }
  }

  const auto main_iterator = functions.find("main");
  if (main_iterator == functions.end()) {
    throw CompileError{SourceLocation{},
                       "program must declare an entry point 'main'"};
  }

  for (const ast::FunctionDeclaration &function : program.functions) {
    std::unordered_set<std::string> type_parameters;
    for (const std::string &parameter : function.type_parameters) {
      if (!type_parameters.insert(parameter).second) {
        throw CompileError{function.location, "type parameter '" + parameter +
                                                  "' is already declared"};
      }
      if (builtin_type(parameter) != nullptr) {
        throw CompileError{function.location,
                           "type parameter '" + parameter +
                               "' conflicts with a built-in type"};
      }
    }

    const SemanticType return_type =
        resolve_type(function.return_type, type_parameters, &class_names);
    if (function.name == "main") {
      if (!function.type_parameters.empty() || !function.parameters.empty() ||
          !return_type.is_concrete() ||
          return_type.concrete->kind() != TypeKind::Int) {
        throw CompileError{
            function.location,
            "entry point must have signature 'def main() : int'"};
      }
    }

    SymbolTable symbols;
    for (const ast::FunctionDeclaration::Parameter &parameter :
         function.parameters) {
      const SemanticType parameter_type =
          resolve_type(parameter.type, type_parameters, &class_names);
      if (!symbols.emplace(parameter.name, Symbol{parameter_type, false, true})
               .second) {
        throw CompileError{parameter.location, "value '" + parameter.name +
                                                   "' is already declared"};
      }
    }

    std::function<SemanticType(const ast::Expression &)> expression_type;
    std::function<void(const ast::Expression &, const SemanticType &,
                       SourceLocation)>
        validate_expression;

    validate_expression = [&](const ast::Expression &expression,
                              const SemanticType &expected,
                              SourceLocation location) {
      const SemanticType actual = expression_type(expression);
      if (same_type(actual, expected))
        return;

      if (expected.is_concrete() &&
          expected.concrete->kind() == TypeKind::Byte) {
        if (const auto *literal =
                std::get_if<ast::IntegerLiteralExpression>(&expression.value)) {
          if (literal->value >= std::numeric_limits<std::int8_t>::min() &&
              literal->value <= std::numeric_limits<std::int8_t>::max()) {
            return;
          }
          throw CompileError{
              literal->location,
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
              const auto iterator = symbols.find(node.name);
              if (iterator == symbols.end()) {
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
                                                   &class_names));
              }
              const std::unordered_set<std::string> callee_parameters{
                  callee.type_parameters.begin(), callee.type_parameters.end()};
              for (std::size_t index = 0; index < node.arguments.size();
                   ++index) {
                SemanticType expected =
                    resolve_type(callee.parameters[index].type,
                                 callee_parameters, &class_names);
                expected = substitute(std::move(expected), substitutions);
                validate_expression(
                    *node.arguments[index], expected,
                    expression_location(*node.arguments[index]));
              }
              return substitute(resolve_type(callee.return_type,
                                             callee_parameters, &class_names),
                                substitutions);
            } else if constexpr (std::is_same_v<Node, ast::NewExpression>) {
              const auto iterator = classes.find(node.class_name);
              if (iterator == classes.end())
                throw CompileError{node.location,
                                   "unknown class '" + node.class_name + "'"};
              const ast::ClassDeclaration &class_declaration =
                  *iterator->second;
              if (node.arguments.size() !=
                  class_declaration.constructor_fields.size())
                throw CompileError{
                    node.location,
                    "constructor '" + node.class_name + "' expects " +
                        std::to_string(
                            class_declaration.constructor_fields.size()) +
                        " argument(s), got " +
                        std::to_string(node.arguments.size())};
              for (std::size_t index = 0; index < node.arguments.size();
                   ++index) {
                const SemanticType field_type = resolve_type(
                    class_declaration.constructor_fields[index].declared_type,
                    {}, &class_names);
                validate_expression(
                    *node.arguments[index], field_type,
                    expression_location(*node.arguments[index]));
              }
              return SemanticType{nullptr, node.class_name, true};
            } else if constexpr (std::is_same_v<Node,
                                                ast::MemberAccessExpression>) {
              const SemanticType object_type = expression_type(*node.object);
              if (!object_type.is_class())
                throw CompileError{node.location,
                                   "member access requires an object"};
              const ast::ClassDeclaration &class_declaration =
                  *classes.at(object_type.parameter);
              for (const auto &field : class_declaration.constructor_fields) {
                if (field.name == node.member)
                  return resolve_type(field.declared_type, {}, &class_names);
              }
              for (const auto &field : class_declaration.fields) {
                if (field.name == node.member)
                  return resolve_type(field.declared_type, {}, &class_names);
              }
              throw CompileError{node.location,
                                 "class '" + class_declaration.name +
                                     "' has no field '" + node.member + "'"};
            } else {
              throw CompileError{node.location,
                                 "method calls are not yet supported"};
            }
          },
          expression.value);
    };

    bool has_return = false;
    for (const ast::Statement &statement : function.body) {
      if (has_return) {
        const SourceLocation location = std::visit(
            [](const auto &node) { return node.location; }, statement);
        throw CompileError{location, "statement after return is unreachable"};
      }

      if (const auto *declaration =
              std::get_if<ast::ValueDeclaration>(&statement)) {
        const SemanticType declared_type = resolve_type(
            declaration->declared_type, type_parameters, &class_names);
        if (symbols.contains(declaration->name)) {
          throw CompileError{declaration->location,
                             "value '" + declaration->name +
                                 "' is already declared"};
        }
        if (declaration->initializer.has_value()) {
          validate_expression(*declaration->initializer, declared_type,
                              declaration->location);
        }
        symbols.emplace(declaration->name,
                        Symbol{declared_type, declaration->is_mutable,
                               declaration->initializer.has_value()});
        continue;
      }

      if (const auto *assignment =
              std::get_if<ast::AssignmentStatement>(&statement)) {
        if (!assignment->object.empty()) {
          const auto object = symbols.find(assignment->object);
          if (object == symbols.end() || !object->second.type.is_class())
            throw CompileError{assignment->location,
                               "field assignment requires an object"};
          if (!object->second.is_initialized)
            throw CompileError{assignment->location, "object '" +
                                                         assignment->object +
                                                         "' is not alive"};
          const ast::ClassDeclaration &class_declaration =
              *classes.at(object->second.type.parameter);
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
                                   "' has no field '" + assignment->name + "'"};
          if (!matched->is_mutable)
            throw CompileError{assignment->location,
                               "cannot assign to immutable field '" +
                                   assignment->name + "'"};
          validate_expression(
              assignment->expression,
              resolve_type(matched->declared_type, {}, &class_names),
              assignment->location);
          continue;
        }
        const auto iterator = symbols.find(assignment->name);
        if (iterator == symbols.end()) {
          throw CompileError{assignment->location,
                             "unknown value '" + assignment->name + "'"};
        }
        if (!iterator->second.is_mutable) {
          throw CompileError{assignment->location,
                             "cannot assign to immutable value '" +
                                 assignment->name + "'"};
        }
        validate_expression(assignment->expression, iterator->second.type,
                            assignment->location);
        iterator->second.is_initialized = true;
        continue;
      }

      if (const auto *deletion =
              std::get_if<ast::DeleteStatement>(&statement)) {
        const SemanticType deleted_type = expression_type(deletion->expression);
        if (!deleted_type.is_class())
          throw CompileError{deletion->location, "delete requires an object"};
        if (const auto *identifier = std::get_if<ast::IdentifierExpression>(
                &deletion->expression.value)) {
          symbols.at(identifier->name).is_initialized = false;
        }
        continue;
      }

      const auto &return_statement = std::get<ast::ReturnStatement>(statement);
      validate_expression(return_statement.expression, return_type,
                          return_statement.location);
      has_return = true;
    }

    if (!has_return) {
      throw CompileError{function.location, "function '" + function.name +
                                                "' must return a value"};
    }
    result.functions.emplace(function.name, std::move(symbols));
  }

  return result;
}

} // namespace janus::semantic
