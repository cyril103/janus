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
  const std::string comments = "def main() : int {\n// keep { this comment "
                               "}\n\n\nreturn 0 // and this\n}\n";
  const janus::driver::FormatOptions compact{2, 0};
  const std::string expected_comments =
      "def main() : int {\n  // keep { this comment }\n  return 0 // and "
      "this\n}\n";
  if (janus::driver::format_source(comments, compact) != expected_comments) {
    std::cerr << "comments or formatter options were not preserved\n";
    return 1;
  }

  const std::string else_if =
      "def choose(first : bool, second : bool) : int {\n"
      "if first {\n"
      "return 1\n"
      "} else if second {\n"
      "return 2\n"
      "} else {\n"
      "return 0\n"
      "}\n"
      "}\n";
  const std::string expected_else_if =
      "def choose(first : bool, second : bool) : int {\n"
      "    if first {\n"
      "        return 1\n"
      "    } else if second {\n"
      "        return 2\n"
      "    } else {\n"
      "        return 0\n"
      "    }\n"
      "}\n";
  const std::string formatted_else_if = janus::driver::format_source(else_if);
  if (formatted_else_if != expected_else_if ||
      janus::driver::format_source(formatted_else_if) != formatted_else_if) {
    std::cerr << "else-if formatting is not canonical and idempotent\n";
    return 1;
  }
  const std::string derivations = "struct Point(val x : int, val y : int)\n"
                                  "derives Copy, Equality, Hashing, Debug {\n"
                                  "}\n";
  const std::string formatted_derivations =
      janus::driver::format_source(derivations);
  if (formatted_derivations != derivations ||
      janus::driver::format_source(formatted_derivations) !=
          formatted_derivations) {
    std::cerr << "derivation clauses are not preserved idempotently\n";
    return 1;
  }
  const std::string imports = "import std.fs as fs\n"
                              "import std.result.{\n"
                              "Result,\n"
                              "Ok as Success\n"
                              "}\n";
  const std::string formatted_imports = "import std.fs as fs\n"
                                        "import std.result.{\n"
                                        "    Result,\n"
                                        "    Ok as Success\n"
                                        "}\n";
  if (janus::driver::format_source(imports) != formatted_imports ||
      janus::driver::format_source(formatted_imports) != formatted_imports) {
    std::cerr << "selective import aliases are not preserved idempotently\n";
    return 1;
  }
  const std::string local_types =
      "def main() : int {\nval inferred=answer()\nval explicit:int=1\nreturn explicit\n}\n";
  const std::string formatted_local_types =
      "def main() : int {\n    val inferred=answer()\n    val explicit:int=1\n    return explicit\n}\n";
  if (janus::driver::format_source(local_types) != formatted_local_types) {
    std::cerr << "formatter changed omitted versus explicit local types:\n"
              << janus::driver::format_source(local_types);
    return 1;
  }
  const std::string constants =
      "const answer : int = 42\n"
      "const def choose(flag : bool) : int {\n"
      "if flag {\nreturn answer\n} else {\nreturn 0\n}\n}\n"
      "staticAssert(choose(true) == 42, \"stable\")\n";
  const std::string formatted_constants =
      "const answer : int = 42\n"
      "const def choose(flag : bool) : int {\n"
      "    if flag {\n        return answer\n    } else {\n"
      "        return 0\n    }\n}\n"
      "staticAssert(choose(true) == 42, \"stable\")\n";
  if (janus::driver::format_source(constants) != formatted_constants ||
      janus::driver::format_source(formatted_constants) !=
          formatted_constants) {
    std::cerr << "constant syntax formatting is not idempotent\n";
    return 1;
  }
  const std::string integer_spellings =
      "def bits() : uint {\nreturn 0xA2_0A + 0B1111_0000 + 1_000\n}\n";
  const std::string formatted_integer_spellings =
      "def bits() : uint {\n    return 0xA2_0A + 0B1111_0000 + 1_000\n}\n";
  if (janus::driver::format_source(integer_spellings) !=
          formatted_integer_spellings ||
      janus::driver::format_source(formatted_integer_spellings) !=
          formatted_integer_spellings) {
    std::cerr << "formatter did not preserve integer base and separators\n";
    return 1;
  }
  const std::string bitwise =
      "def bits(value : ubyte) : ubyte {\nreturn (value&ubyte(15))<<1|value>>2^ubyte(3)\n}\n";
  const std::string formatted_bitwise = janus::driver::format_source(bitwise);
  if (janus::driver::format_source(formatted_bitwise) != formatted_bitwise) {
    std::cerr << "bitwise formatting is not idempotent\n";
    return 1;
  }
  std::cout << "Janus formatting is deterministic\n";
  return 0;
}
