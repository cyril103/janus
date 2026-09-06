#pragma once

#include <iosfwd>
#include <string_view>

namespace janus::cli {

void print_usage(std::ostream &output);
void print_command_usage(std::ostream &output, std::string_view command);
[[nodiscard]] bool has_command_help(std::string_view command);
int explain_diagnostic(int argc, char **argv);

} // namespace janus::cli
