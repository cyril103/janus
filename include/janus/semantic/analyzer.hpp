#pragma once

#include "janus/ast/ast.hpp"
#include "janus/constant/evaluator.hpp"

#include <string>
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
               bool is_function_type = false)
      : concrete{concrete_type}, parameter{std::move(parameter_name)},
        class_type{is_class_type}, type_arguments{std::move(arguments)},
        pointer_type{is_pointer_type}, enum_type{is_enum_type},
        function_type{is_function_type} {}

  const Type *concrete{};
  std::string parameter;
  bool class_type{};
  std::vector<SemanticType> type_arguments;
  bool pointer_type{};
  bool enum_type{};
  bool function_type{};

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
  SymbolTable globals;
  std::unordered_map<std::string, SymbolTable> functions;
  std::unordered_map<const ast::MemberAccessExpression *, std::string>
      qualified_global_reads;
  std::unordered_map<const ast::AssignmentStatement *, std::string>
      qualified_global_writes;
  std::unordered_map<const ast::ValueDeclaration *, SemanticType> local_types;
  std::unordered_map<const ast::ValueDeclaration *, constant::Value>
      local_constant_values;
  std::unordered_map<const ast::Expression *, std::vector<SemanticType>>
      inferred_generic_arguments;
  std::vector<Diagnostic> diagnostics;
};

struct AnalysisOptions {
  bool require_entry_point{true};
};

class Analyzer final {
public:
  [[nodiscard]] AnalysisResult
  analyze(const ast::Program &program,
          AnalysisOptions options = {}) const;
};

} // namespace janus::semantic
