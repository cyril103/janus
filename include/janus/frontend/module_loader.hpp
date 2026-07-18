#pragma once

#include "janus/ast/ast.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace janus::frontend {

class ModuleLoader final {
public:
  explicit ModuleLoader(std::vector<std::filesystem::path> search_paths = {});

  [[nodiscard]] ast::Program load(const std::filesystem::path &entry_path);

private:
  [[nodiscard]] ast::Program
  load_file(const std::filesystem::path &path,
            const std::filesystem::path &project_root,
            const std::string *expected_module);
  [[nodiscard]] std::filesystem::path
  resolve_import(std::string_view module,
                 const std::filesystem::path &project_root) const;

  std::vector<std::filesystem::path> search_paths_;
  std::vector<std::filesystem::path> loaded_paths_;
};

} // namespace janus::frontend
