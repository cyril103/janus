#pragma once

#include "janus/ast/ast.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace janus::frontend {

struct ModuleLoadTimings {
  std::chrono::nanoseconds loading{};
  std::chrono::nanoseconds parsing{};
};

class ModuleLoader final {
public:
  explicit ModuleLoader(std::vector<std::filesystem::path> search_paths = {});

  void set_source_override(const std::filesystem::path &path,
                           std::string source);

  [[nodiscard]] ast::Program load(const std::filesystem::path &entry_path,
                                  ModuleLoadTimings *timings = nullptr);
  [[nodiscard]] ast::Program load(const std::filesystem::path &entry_path,
                                  std::string_view entry_source,
                                  ModuleLoadTimings *timings = nullptr);

private:
  [[nodiscard]] const ast::Program &
  load_file(const std::filesystem::path &path,
            const std::filesystem::path &project_root,
            const std::string *expected_module,
            const std::string_view *source_override,
            ModuleLoadTimings *timings);
  [[nodiscard]] ast::Program
  take_loaded_program(const std::filesystem::path &entry_path);
  [[nodiscard]] std::filesystem::path
  resolve_import(std::string_view module,
                 const std::filesystem::path &project_root) const;

  std::vector<std::filesystem::path> search_paths_;
  std::unordered_map<std::filesystem::path, std::string> source_overrides_;
  std::unordered_set<std::filesystem::path> visiting_paths_;
  std::unordered_map<std::filesystem::path, std::unique_ptr<ast::Program>>
      loaded_programs_;
  std::vector<std::filesystem::path> load_order_;
};

} // namespace janus::frontend
