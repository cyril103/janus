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
  std::cout << "Janus formatting is deterministic\n";
  return 0;
}
