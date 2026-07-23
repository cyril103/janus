#pragma once

#include "janus/diagnostics/compile_error.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

class Server final {
public:
  explicit Server(std::vector<std::filesystem::path> module_search_paths = {});

  [[nodiscard]] std::vector<std::string> handle(std::string_view message);

private:
  [[nodiscard]] std::string diagnostics(std::string_view uri,
                                        std::string_view source) const;

  std::vector<std::filesystem::path> module_search_paths_;
  std::unordered_map<std::string, std::string> documents_;
  std::unordered_map<std::string, DocumentIndex> index_cache_;
  bool shutdown_{};
};

} // namespace janus::lsp
