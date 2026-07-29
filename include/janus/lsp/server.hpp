#pragma once

#include "janus/diagnostics/compile_error.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace janus::lsp {

struct IndexedSymbol {
  std::string id;
  std::string name;
  std::string detail;
  SourceLocation location;
  std::size_t scope_start{};
  std::size_t scope_end{};
  std::size_t scope_depth{};
  bool is_global{};
  bool is_top_level{};
  bool is_private{};
  std::optional<std::string> module_name;
};

struct DocumentIndex {
  std::string source;
  std::vector<IndexedSymbol> symbols;
};

struct WorkspaceIndexMetrics {
  std::size_t files{};
  std::size_t symbols{};
  std::size_t source_bytes{};
  std::size_t estimated_memory_bytes{};
  std::uint64_t startup_milliseconds{};
};

class Server final {
public:
  explicit Server(std::vector<std::filesystem::path> module_search_paths = {});

  [[nodiscard]] std::vector<std::string> handle(std::string_view message);
  [[nodiscard]] const WorkspaceIndexMetrics &
  workspace_index_metrics() const noexcept {
    return workspace_metrics_;
  }

private:
  [[nodiscard]] std::vector<Diagnostic>
  analyze_document(std::string_view uri, std::string_view source) const;
  [[nodiscard]] std::string diagnostics(std::string_view uri,
                                        std::string_view source) const;
  void initialize_workspace(const std::vector<std::filesystem::path> &roots);
  void index_workspace_file(const std::filesystem::path &path, bool dependency);
  void remove_workspace_file(std::string_view uri);
  void refresh_workspace_metrics(std::uint64_t startup_milliseconds = 0);

  std::vector<std::filesystem::path> module_search_paths_;
  std::vector<std::filesystem::path> workspace_search_paths_;
  std::vector<std::filesystem::path> workspace_roots_;
  std::vector<std::filesystem::path> dependency_roots_;
  std::unordered_map<std::string, std::string> documents_;
  std::unordered_map<std::string, std::int64_t> document_versions_;
  std::unordered_map<std::string, DocumentIndex> index_cache_;
  std::unordered_set<std::string> workspace_uris_;
  std::unordered_set<std::string> dependency_uris_;
  WorkspaceIndexMetrics workspace_metrics_;
  bool inferred_type_hints_{true};
  bool shutdown_{};
};

} // namespace janus::lsp
