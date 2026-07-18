#include "janus/semantic/analyzer.hpp"

#include "janus/diagnostics/compile_error.hpp"

#include <string>
#include <variant>

namespace janus::semantic {

SymbolTable Analyzer::analyze(const ast::Program &program) const {
  SymbolTable symbols;

  for (const ast::ValueDeclaration &declaration : program.declarations) {
    if (symbols.contains(declaration.name)) {
      throw CompileError{declaration.location, "value '" + declaration.name +
                                                   "' is already declared"};
    }

    const Type &initializer_type = expression_type(declaration.initializer);
    if (declaration.declared_type->kind() != initializer_type.kind()) {
      throw CompileError{declaration.location,
                         "cannot initialize value of type '" +
                             std::string{declaration.declared_type->name()} +
                             "' with an expression of type '" +
                             std::string{initializer_type.name()} + "'"};
    }

    symbols.emplace(declaration.name,
                    Symbol{declaration.declared_type, declaration.is_mutable});
  }

  return symbols;
}

const Type &
Analyzer::expression_type(const ast::Expression &expression) const noexcept {
  return std::visit(
      [](const auto &) -> const Type & { return Type::int_type(); },
      expression);
}

} // namespace janus::semantic
