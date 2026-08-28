#pragma once

#include "janus/ast/ast.hpp"
#include "janus/constant/evaluator.hpp"
#include "janus/target/target.hpp"

#include <string>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace janus::semantic {

struct SemanticType {
  SemanticType() = default;
  SemanticType(const Type *concrete_type, std::string parameter_name = {},
               bool is_class_type = false,
               std::vector<SemanticType> arguments = {},
               bool is_pointer_type = false, bool is_enum_type = false,
               bool is_function_type = false,
               std::vector<ast::ParameterOwnership> parameter_ownership = {},
               ast::ReturnOwnership return_ownership =
                   ast::ReturnOwnership::Unspecified)
      : concrete{concrete_type}, parameter{std::move(parameter_name)},
        class_type{is_class_type}, type_arguments{std::move(arguments)},
        pointer_type{is_pointer_type}, enum_type{is_enum_type},
        function_type{is_function_type},
        function_parameter_ownership{std::move(parameter_ownership)},
        function_return_ownership{return_ownership} {}

  const Type *concrete{};
  std::string parameter;
  bool class_type{};
  std::vector<SemanticType> type_arguments;
  bool pointer_type{};
  bool enum_type{};
  bool function_type{};
  std::vector<ast::ParameterOwnership> function_parameter_ownership;
  ast::ReturnOwnership function_return_ownership{
      ast::ReturnOwnership::Unspecified};

  [[nodiscard]] bool is_concrete() const noexcept {
    return concrete != nullptr;
  }
  [[nodiscard]] bool is_class() const noexcept { return class_type; }
  [[nodiscard]] bool is_pointer() const noexcept { return pointer_type; }
  [[nodiscard]] bool is_enum() const noexcept { return enum_type; }
  [[nodiscard]] bool is_function() const noexcept { return function_type; }
  [[nodiscard]] std::string name() const;
};

struct Symbol {
  Symbol() = default;
  Symbol(SemanticType symbol_type, bool mutable_value, bool initialized)
      : type{std::move(symbol_type)}, is_mutable{mutable_value},
        is_initialized{initialized}, may_be_initialized{initialized} {}

  SemanticType type;
  bool is_mutable{};
  bool is_initialized{};
  bool may_be_initialized{};
};

using SymbolTable = std::unordered_map<std::string, Symbol>;

struct AnalysisResult {
  struct IndexedCapabilities {
    SemanticType element_type;
    const ast::FunctionDeclaration *read{};
    const ast::FunctionDeclaration *replace{};
  };
  struct InferredLambdaParameter {
    std::optional<std::string> module_name;
    SourceLocation location;
    SemanticType type;
  };
  Target target;
  SymbolTable globals;
  std::unordered_map<std::string, SymbolTable> functions;
  std::unordered_map<const ast::MemberAccessExpression *, std::string>
      qualified_global_reads;
  std::unordered_map<const ast::AssignmentStatement *, std::string>
      qualified_global_writes;
  std::unordered_map<const ast::ValueDeclaration *, SemanticType> local_types;
  std::unordered_map<const ast::ValueDeclaration *, constant::Value>
      local_constant_values;
  std::unordered_map<std::string, constant::Value> global_constant_values;
  // Keeps nominal types referenced by compile-time aggregate values alive.
  std::vector<std::shared_ptr<Type>> constant_value_types;
  std::unordered_map<const ast::Expression *, std::vector<SemanticType>>
      inferred_generic_arguments;
  std::vector<InferredLambdaParameter> inferred_lambda_parameters;
  std::unordered_map<const ast::Expression *, ast::ReturnOwnership>
      call_return_ownership;
  std::unordered_map<const ast::IndexExpression *, IndexedCapabilities>
      indexed_capabilities;
  std::vector<Diagnostic> diagnostics;
};

struct AnalysisOptions {
  bool require_entry_point{true};
  std::size_t constant_step_budget{10000};
  std::size_t constant_recursion_budget{128};
  std::size_t constant_memory_budget{16 * 1024 * 1024};
  std::size_t constant_value_size_budget{1024 * 1024};
  Target target;
};

class Analyzer final {
public:
  [[nodiscard]] AnalysisResult
  analyze(const ast::Program &program,
          AnalysisOptions options = {}) const;
};

} // namespace janus::semantic
