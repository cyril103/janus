#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace janus::lsp {

class Server final {
public:
  [[nodiscard]] std::vector<std::string> handle(std::string_view message);

private:
  [[nodiscard]] std::string diagnostics(std::string_view uri,
                                        std::string_view source) const;

  std::unordered_map<std::string, std::string> documents_;
  bool shutdown_{};
};

} // namespace janus::lsp
