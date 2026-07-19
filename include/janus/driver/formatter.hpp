#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace janus::driver {

struct FormatOptions {
  std::size_t indent_width{4};
  std::size_t max_blank_lines{1};
};

[[nodiscard]] FormatOptions
load_format_options(const std::filesystem::path &path);
[[nodiscard]] std::string format_source(std::string_view source,
                                        const FormatOptions &options = {});

} // namespace janus::driver
