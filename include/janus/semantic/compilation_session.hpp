#pragma once

#include "janus/frontend/module_loader.hpp"
#include "janus/semantic/analyzer.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace janus::semantic {

struct CompilationSessionTimings {
  std::chrono::nanoseconds loading{};
  std::chrono::nanoseconds parsing{};
  std::chrono::nanoseconds analysis{};
};

struct AnalyzedProgram {
  ast::Program program;
  AnalysisResult analysis;
  CompilationSessionTimings timings;
};

// Shared frontend entry point. It owns configuration and editor overlays while
// every request gets a fresh loader, so a failed request cannot poison the
// following one.
class CompilationSession final {
public:
  explicit CompilationSession(
      std::vector<std::filesystem::path> search_paths = {},
      AnalysisOptions options = {}, bool infer_entry_point = false);

  void set_source_override(const std::filesystem::path &path,
                           std::string source);
  void clear_source_override(const std::filesystem::path &path);

  [[nodiscard]] AnalyzedProgram
  analyze(const std::filesystem::path &entry_path) const;
  [[nodiscard]] AnalyzedProgram analyze(const std::filesystem::path &entry_path,
                                        std::string_view entry_source) const;
  [[nodiscard]] AnalysisResult analyze(const ast::Program &program) const;

private:
  [[nodiscard]] AnalyzedProgram
  analyze_loaded(frontend::ModuleLoader &loader,
                 const std::filesystem::path &entry_path,
                 const std::string_view *entry_source) const;

  std::vector<std::filesystem::path> search_paths_;
  AnalysisOptions options_;
  bool infer_entry_point_{};
  std::unordered_map<std::filesystem::path, std::string> source_overrides_;
};

} // namespace janus::semantic
