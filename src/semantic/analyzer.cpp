#include "janus/semantic/analyzer.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <string>
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

        const Type &initializer_type =
            expression_type(declaration->initializer);
        if (declaration->declared_type->kind() != initializer_type.kind()) {
          throw CompileError{
              declaration->location,
              "cannot initialize value of type '" +
                  std::string{declaration->declared_type->name()} +
                  "' with an expression of type '" +
                  std::string{initializer_type.name()} + "'"};
        }

        symbols.emplace(declaration->name, Symbol{declaration->declared_type,
                                                  declaration->is_mutable});
        continue;
      }

      const auto &return_statement = std::get<ast::ReturnStatement>(statement);
      const Type &returned_type = expression_type(return_statement.expression);
      if (returned_type.kind() != function.return_type->kind()) {
        throw CompileError{return_statement.location,
                           "return type does not match function return type"};
      }
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

const Type &
Analyzer::expression_type(const ast::Expression &expression) const noexcept {
  return std::visit(
      [](const auto &) -> const Type & { return Type::int_type(); },
      expression);
}

} // namespace janus::semantic
