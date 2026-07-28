#pragma once

#include "janus/ast/ast.hpp"
#include "janus/driver/manifest.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace janus::driver {

struct DocumentationOptions {
  std::string package_name;
  std::string package_version;
  std::filesystem::path output_directory;
};

struct UnresolvedDocumentationLink {
  std::string symbol;
  std::string context;
};

struct DocumentationReport {
  std::filesystem::path index_path;
  std::filesystem::path api_index_path;
  std::vector<UnresolvedDocumentationLink> unresolved_links;
  std::size_t module_count{};
  std::size_t symbol_count{};
};

[[nodiscard]] DocumentationReport
generate_documentation(const std::vector<ast::Program> &programs,
                       const DocumentationOptions &options);

[[nodiscard]] DocumentationReport
generate_package_documentation(const Manifest &manifest,
                               const std::filesystem::path &output_directory);

void open_documentation(const std::filesystem::path &index_path);

} // namespace janus::driver
