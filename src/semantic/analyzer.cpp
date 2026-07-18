#include "janus/semantic/analyzer.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>

namespace janus::semantic {

AnalysisResult Analyzer::analyze(const ast::Program &program) const {
  AnalysisResult result;
  bool has_main = false;

  for (const ast::FunctionDeclaration &function : program.functions) {
    if (result.functions.contains(function.name)) {
      throw CompileError{function.location, "function '" + function.name +
                                                "' is already declared"};
    }

    if (function.name == "main") {
      has_main = true;
      if (function.return_type->kind() != TypeKind::Int) {
        throw CompileError{function.location,
                           "entry point 'main' must return int"};
      }
    }

    SymbolTable symbols;
    bool has_return = false;

    for (const ast::Statement &statement : function.body) {
      if (has_return) {
        const SourceLocation location = std::visit(
            [](const auto &node) { return node.location; }, statement);
        throw CompileError{location, "statement after return is unreachable"};
      }

      if (const auto *declaration =
              std::get_if<ast::ValueDeclaration>(&statement)) {
        if (symbols.contains(declaration->name)) {
          throw CompileError{declaration->location,
                             "value '" + declaration->name +
                                 "' is already declared"};
        }

        validate_expression(declaration->initializer,
                            *declaration->declared_type, declaration->location);

        symbols.emplace(declaration->name, Symbol{declaration->declared_type,
                                                  declaration->is_mutable});
        continue;
      }

      const auto &return_statement = std::get<ast::ReturnStatement>(statement);
      validate_expression(return_statement.expression, *function.return_type,
                          return_statement.location);
      has_return = true;
    }

    if (!has_return) {
      throw CompileError{function.location, "function '" + function.name +
                                                "' must return a value"};
    }

    result.functions.emplace(function.name, std::move(symbols));
  }

  if (!has_main) {
    throw CompileError{SourceLocation{},
                       "program must declare an entry point 'main'"};
  }

  return result;
}

void Analyzer::validate_expression(const ast::Expression &expression,
                                   const Type &expected_type,
                                   SourceLocation location) const {
  const Type &actual_type = expression_type(expression);
  if (actual_type.kind() == expected_type.kind()) {
    return;
  }

  if (expected_type.kind() == TypeKind::Byte) {
    if (const auto *literal =
            std::get_if<ast::IntegerLiteralExpression>(&expression)) {
      if (literal->value >= std::numeric_limits<std::int8_t>::min() &&
          literal->value <= std::numeric_limits<std::int8_t>::max()) {
        return;
      }
      throw CompileError{literal->location,
                         "integer literal is outside the signed 8-bit range"};
    }
  }

  throw CompileError{location,
                     "cannot use expression of type '" +
                         std::string{actual_type.name()} + "' where type '" +
                         std::string{expected_type.name()} + "' is required"};
}

const Type &
Analyzer::expression_type(const ast::Expression &expression) const noexcept {
  return std::visit(
      [](const auto &literal) -> const Type & {
        using Literal = std::decay_t<decltype(literal)>;
        if constexpr (std::is_same_v<Literal, ast::IntegerLiteralExpression>) {
          return Type::int_type();
        } else if constexpr (std::is_same_v<Literal,
                                            ast::DoubleLiteralExpression>) {
          return Type::double_type();
        } else if constexpr (std::is_same_v<Literal,
                                            ast::CharacterLiteralExpression>) {
          return Type::char_type();
        } else if constexpr (std::is_same_v<Literal,
                                            ast::BooleanLiteralExpression>) {
          return Type::bool_type();
        } else {
          return Type::string_type();
        }
      },
      expression);
}

} // namespace janus::semantic
