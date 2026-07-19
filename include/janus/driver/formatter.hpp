#pragma once

#include <string>
#include <string_view>

namespace janus::driver {

[[nodiscard]] std::string format_source(std::string_view source);

} // namespace janus::driver
