#include "janus/semantic/compilation_session.hpp"

#include <chrono>
#include <utility>

namespace janus::semantic {

CompilationSession::CompilationSession(
    std::vector<std::filesystem::path> search_paths, AnalysisOptions options,
    bool infer_entry_point)
    : search_paths_{std::move(search_paths)}, options_{std::move(options)},
      infer_entry_point_{infer_entry_point} {}

void CompilationSession::set_source_override(
    const std::filesystem::path &path, std::string source) {
  source_overrides_.insert_or_assign(path.lexically_normal(),
                                     std::move(source));
}

void CompilationSession::clear_source_override(
    const std::filesystem::path &path) {
  source_overrides_.erase(path.lexically_normal());
}

AnalyzedProgram
CompilationSession::analyze(const std::filesystem::path &entry_path) const {
  frontend::ModuleLoader loader{search_paths_};
  for (const auto &[path, source] : source_overrides_)
    loader.set_source_override(path, source);
  return analyze_loaded(loader, entry_path, nullptr);
}

AnalyzedProgram CompilationSession::analyze(
    const std::filesystem::path &entry_path, std::string_view entry_source) const {
  frontend::ModuleLoader loader{search_paths_};
  for (const auto &[path, source] : source_overrides_)
    loader.set_source_override(path, source);
  return analyze_loaded(loader, entry_path, &entry_source);
}

AnalysisResult CompilationSession::analyze(const ast::Program &program) const {
  return Analyzer{}.analyze(program, options_);
}

AnalyzedProgram CompilationSession::analyze_loaded(
    frontend::ModuleLoader &loader, const std::filesystem::path &entry_path,
    const std::string_view *entry_source) const {
  AnalyzedProgram result;
  frontend::ModuleLoadTimings load_timings;
  if (entry_source == nullptr)
    result.program = loader.load(entry_path, &load_timings);
  else
    result.program = loader.load(entry_path, *entry_source, &load_timings);
  result.timings.loading = load_timings.loading;
  result.timings.parsing = load_timings.parsing;
  const auto analysis_start = std::chrono::steady_clock::now();
  AnalysisOptions options = options_;
  if (infer_entry_point_)
    options.require_entry_point = !result.program.module_name.has_value();
  result.analysis = Analyzer{}.analyze(result.program, std::move(options));
  result.timings.analysis = std::chrono::steady_clock::now() - analysis_start;
  return result;
}

} // namespace janus::semantic
