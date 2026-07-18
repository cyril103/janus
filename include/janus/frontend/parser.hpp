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
  [[nodiscard]] ast::ExpressionStatement parse_expression_statement();
  [[nodiscard]] std::shared_ptr<ast::IfStatement> parse_if_statement();
  [[nodiscard]] std::shared_ptr<ast::WhileStatement> parse_while_statement();
  [[nodiscard]] ast::Expression parse_expression();
  [[nodiscard]] ast::Expression parse_logical_or();
  [[nodiscard]] ast::Expression parse_logical_and();
  [[nodiscard]] ast::Expression parse_equality();
  [[nodiscard]] ast::Expression parse_comparison();
  [[nodiscard]] ast::Expression parse_additive();
  [[nodiscard]] ast::Expression parse_multiplicative();
  [[nodiscard]] ast::Expression parse_unary();
  [[nodiscard]] ast::Expression parse_primary();
  [[nodiscard]] ast::TypeReference parse_type();
  [[nodiscard]] Token expect(TokenKind kind);
  [[nodiscard]] bool starts_assignment() const;
  void advance();

  Lexer lexer_;
  Token current_;
};

} // namespace janus::frontend
