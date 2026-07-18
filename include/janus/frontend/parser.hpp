#pragma once

#include "janus/ast/ast.hpp"
#include "janus/frontend/lexer.hpp"

#include <string_view>

namespace janus::frontend {

class Parser final {
public:
  explicit Parser(std::string_view source);

  [[nodiscard]] ast::Program parse_program();

private:
  [[nodiscard]] ast::ClassDeclaration parse_class_declaration();
  [[nodiscard]] ast::DestructorDeclaration parse_destructor_declaration();
  [[nodiscard]] std::vector<ast::Statement> parse_block();
  [[nodiscard]] ast::FunctionDeclaration parse_function_declaration();
  [[nodiscard]] ast::Statement parse_statement();
  [[nodiscard]] ast::ValueDeclaration parse_variable_declaration();
  [[nodiscard]] ast::AssignmentStatement parse_assignment_statement();
  [[nodiscard]] ast::DeleteStatement parse_delete_statement();
  [[nodiscard]] ast::ReturnStatement parse_return_statement();
  [[nodiscard]] ast::Expression parse_expression();
  [[nodiscard]] ast::TypeReference parse_type();
  [[nodiscard]] Token expect(TokenKind kind);
  void advance();

  Lexer lexer_;
  Token current_;
};

} // namespace janus::frontend
