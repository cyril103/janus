#pragma once

#include "janus/ast/ast.hpp"

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
  SemanticType type;
  bool is_mutable;
  bool is_initialized;
};

using SymbolTable = std::unordered_map<std::string, Symbol>;

struct AnalysisResult {
  std::unordered_map<std::string, SymbolTable> functions;
};

class Analyzer final {
public:
  [[nodiscard]] AnalysisResult analyze(const ast::Program &program) const;
};

} // namespace janus::semantic
