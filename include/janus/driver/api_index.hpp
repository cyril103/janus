#pragma once

#include "janus/ast/ast.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace janus::driver {

inline constexpr std::uint32_t api_index_format_version = 1;

struct ApiParameter {
  std::string name;
  std::string type;
  std::string documentation;

  bool operator==(const ApiParameter &) const = default;
};

struct ApiSymbol {
  std::string simple_name;
  std::string qualified_name;
  std::string package;
  std::string module;
  std::string required_import;
  std::string kind;
  std::string signature;
  std::vector<std::string> generic_parameters;
  std::vector<std::string> generic_constraints;
  std::vector<ApiParameter> parameters;
  std::string return_type;
  std::string summary;
  std::string documentation;
  std::string visibility{"public"};
  std::string documentation_link;
  bool deprecated{};
  std::optional<std::string> replacement;
};

struct ApiIndex {
  std::uint32_t format_version{api_index_format_version};
  std::string package;
  std::string package_version;
  std::vector<ApiSymbol> symbols;
};

struct ApiIndexMetadata {
  std::string package;
  std::string package_version;
};

struct ApiSearchQuery {
  ApiSearchQuery(
      std::string text_value, std::string module_value = {},
      std::string kind_value = {}, std::string package_value = {},
      std::optional<std::string> expected_type_value = std::nullopt,
      std::optional<std::size_t> argument_count_value = std::nullopt,
      std::vector<std::string> imported_modules_value = {},
      std::optional<std::size_t> generic_argument_count_value = std::nullopt)
      : text(std::move(text_value)), module(std::move(module_value)),
        kind(std::move(kind_value)), package(std::move(package_value)),
        expected_type(std::move(expected_type_value)),
        argument_count(argument_count_value),
        imported_modules(std::move(imported_modules_value)),
        generic_argument_count(generic_argument_count_value) {}

  std::string text;
  std::string module;
  std::string kind;
  std::string package;
  std::optional<std::string> expected_type;
  std::optional<std::size_t> argument_count;
  std::vector<std::string> imported_modules;
  std::optional<std::size_t> generic_argument_count;
};

struct ApiSearchResult {
  const ApiSymbol *symbol{};
  std::int64_t score{};
  std::string reason;
};

[[nodiscard]] ApiIndex
build_api_index(const std::vector<ast::Program> &programs,
                const ApiIndexMetadata &metadata);
[[nodiscard]] ApiIndex build_api_index_from_source_roots(
    const std::vector<std::filesystem::path> &roots,
    const ApiIndexMetadata &metadata);
[[nodiscard]] ApiIndex merge_api_indexes(const std::vector<ApiIndex> &indexes);
[[nodiscard]] std::vector<ApiSearchResult>
search_api(const ApiIndex &index, const ApiSearchQuery &query);
[[nodiscard]] std::string serialize_api_index(const ApiIndex &index);
[[nodiscard]] ApiIndex parse_api_index(std::string_view json);
[[nodiscard]] ApiIndex load_api_index(const std::filesystem::path &path);
void write_api_index(const ApiIndex &index, const std::filesystem::path &path);
[[nodiscard]] std::string
format_api_search(const std::vector<ApiSearchResult> &results,
                  std::string_view format);

} // namespace janus::driver
