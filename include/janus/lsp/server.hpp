#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace janus::lsp {

class Server final {
public:
  explicit Server(std::vector<std::filesystem::path> module_search_paths = {});

  [[nodiscard]] std::vector<std::string> handle(std::string_view message);

private:
  [[nodiscard]] std::string diagnostics(std::string_view uri,
                                        std::string_view source) const;

  std::vector<std::filesystem::path> module_search_paths_;
  std::unordered_map<std::string, std::string> documents_;
  bool shutdown_{};
};

} // namespace janus::lsp
