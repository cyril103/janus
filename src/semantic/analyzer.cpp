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
             const std::unordered_set<std::string> &type_parameters) {
  if (const janus::Type *type = builtin_type(reference.name)) {
    return janus::semantic::SemanticType{type, {}};
  }
  if (type_parameters.contains(reference.name)) {
    return janus::semantic::SemanticType{nullptr, reference.name};
  }
  throw janus::CompileError{reference.location,
                            "unknown type '" + reference.name + "'"};
}

bool same_type(const janus::semantic::SemanticType &left,
               const janus::semantic::SemanticType &right) {
  if (left.is_concrete() != right.is_concrete())
    return false;
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
        resolve_type(function.return_type, type_parameters);
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
          resolve_type(parameter.type, type_parameters);
      if (!symbols.emplace(parameter.name, Symbol{parameter_type, false})
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
              return iterator->second.type;
            } else {
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
                substitutions.emplace(
                    callee.type_parameters[index],
                    resolve_type(node.type_arguments[index], type_parameters));
              }
              const std::unordered_set<std::string> callee_parameters{
                  callee.type_parameters.begin(), callee.type_parameters.end()};
              for (std::size_t index = 0; index < node.arguments.size();
                   ++index) {
                SemanticType expected = resolve_type(
                    callee.parameters[index].type, callee_parameters);
                expected = substitute(std::move(expected), substitutions);
                validate_expression(
                    *node.arguments[index], expected,
                    expression_location(*node.arguments[index]));
              }
              return substitute(
                  resolve_type(callee.return_type, callee_parameters),
                  substitutions);
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
        const SemanticType declared_type =
            resolve_type(declaration->declared_type, type_parameters);
        if (symbols.contains(declaration->name)) {
          throw CompileError{declaration->location,
                             "value '" + declaration->name +
                                 "' is already declared"};
        }
        validate_expression(declaration->initializer, declared_type,
                            declaration->location);
        symbols.emplace(declaration->name,
                        Symbol{declared_type, declaration->is_mutable});
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
