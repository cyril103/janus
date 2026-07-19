#include "janus/driver/formatter.hpp"

#include <iostream>
#include <string>

int main() {
  const std::string source = "def main() : int {  \n"
                             " val text : string = \"{ unchanged }\"\n"
                             "if true {\n"
                             "return 0\n"
                             "}\n"
                             "}\n";
  const std::string expected = "def main() : int {\n"
                               "    val text : string = \"{ unchanged }\"\n"
                               "    if true {\n"
                               "        return 0\n"
                               "    }\n"
                               "}\n";
  const std::string formatted = janus::driver::format_source(source);
  if (formatted != expected) {
    std::cerr << "unexpected formatted source:\n" << formatted;
    return 1;
  }
  if (janus::driver::format_source(formatted) != formatted) {
    std::cerr << "formatting is not idempotent\n";
    return 1;
  }
  const std::string comments =
      "def main() : int {\n// keep { this comment }\n\n\nreturn 0 // and this\n}\n";
  const janus::driver::FormatOptions compact{2, 0};
  const std::string expected_comments =
      "def main() : int {\n  // keep { this comment }\n  return 0 // and this\n}\n";
  if (janus::driver::format_source(comments, compact) != expected_comments) {
    std::cerr << "comments or formatter options were not preserved\n";
    return 1;
  }
  std::cout << "Janus formatting is deterministic\n";
  return 0;
}
