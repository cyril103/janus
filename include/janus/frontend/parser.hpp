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
  [[nodiscard]] ast::TraitDeclaration parse_trait_declaration();
  [[nodiscard]] ast::FunctionDeclaration parse_trait_method();
  [[nodiscard]] ast::EnumDeclaration parse_enum_declaration();
  [[nodiscard]] ast::ClassDeclaration parse_class_declaration();
  [[nodiscard]] std::vector<ast::Derivation> parse_derivations();
  [[nodiscard]] ast::DestructorDeclaration parse_destructor_declaration();
  [[nodiscard]] std::vector<ast::Statement> parse_block();
  [[nodiscard]] ast::FunctionDeclaration
  parse_function_declaration(bool is_constant = false);
  [[nodiscard]] ast::Statement parse_statement();
  [[nodiscard]] ast::ValueDeclaration
  parse_variable_declaration(bool is_constant = false,
                             SourceLocation constant_location = {});
  [[nodiscard]] ast::Program::StaticAssertion parse_static_assertion();
  [[nodiscard]] ast::AssignmentStatement parse_assignment_statement();
  [[nodiscard]] ast::DeleteStatement parse_delete_statement();
  [[nodiscard]] ast::DeferStatement parse_defer_statement();
  [[nodiscard]] ast::BreakStatement parse_break_statement();
  [[nodiscard]] ast::ContinueStatement parse_continue_statement();
  [[nodiscard]] ast::ReturnStatement parse_return_statement();
  [[nodiscard]] ast::ExpressionStatement parse_expression_statement();
  [[nodiscard]] std::shared_ptr<ast::IfStatement> parse_if_statement();
  [[nodiscard]] std::shared_ptr<ast::WhileStatement> parse_while_statement();
  [[nodiscard]] std::shared_ptr<ast::ForStatement> parse_for_statement();
  [[nodiscard]] ast::Expression parse_expression();
  [[nodiscard]] ast::Expression parse_logical_or();
  [[nodiscard]] ast::Expression parse_logical_and();
  [[nodiscard]] ast::Expression parse_equality();
  [[nodiscard]] ast::Expression parse_comparison();
  [[nodiscard]] ast::Expression parse_additive();
  [[nodiscard]] ast::Expression parse_multiplicative();
  [[nodiscard]] ast::Expression parse_unary();
  [[nodiscard]] ast::Expression parse_primary();
  [[nodiscard]] ast::Expression parse_postfix(ast::Expression expression);
  [[nodiscard]] ast::TypeReference parse_type();
  [[nodiscard]] ast::ImportDeclaration parse_import_declaration();
  [[nodiscard]] bool starts_lambda() const;
  [[nodiscard]] std::string parse_qualified_name();
  [[nodiscard]] std::string take_documentation();
  [[nodiscard]] Token expect(TokenKind kind);
  [[nodiscard]] bool starts_assignment() const;
  void synchronize_top_level();
  void advance();

  Lexer lexer_;
  Token current_;
};

} // namespace janus::frontend
