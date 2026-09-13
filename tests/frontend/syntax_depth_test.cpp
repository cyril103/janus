#include "janus/frontend/parser.hpp"
#include "../support/require.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {
std::string repeat(std::string_view text, std::size_t count) {
  std::string result;
  while (count--)
    result += text;
  return result;
}

std::string body(std::string expression) {
  return "def main() : int { return " + expression + " }";
}

void check(const std::string &source, bool accepted) {
  try {
    janus::frontend::Parser parser{source};
    const auto program = parser.parse_program();
    JANUS_REQUIRE(accepted);
    JANUS_REQUIRE(!program.functions.empty());
  } catch (const janus::CompileError &error) {
    if (accepted)
      std::cerr << "Unexpected rejection: " << source.substr(0, 160) << "\n";
    JANUS_REQUIRE(!accepted);
    JANUS_REQUIRE(error.diagnostics().size() == 1);
    JANUS_REQUIRE(error.diagnostic().code ==
                  janus::DiagnosticCode::ParserSyntaxDepthExceeded);
    JANUS_REQUIRE(error.diagnostic().primary_location.offset < source.size());
    JANUS_REQUIRE(std::string_view{error.what()} ==
                  "maximum syntax depth exceeded (limit: 128)");
  }
}
} // namespace

int main() {
  constexpr auto limit = janus::frontend::Parser::max_syntax_depth;
  // Exact boundaries: one level for the function block and one for the leaf.
  for (const auto n : {std::size_t{32}, limit - 2, limit - 1, std::size_t{5000}}) {
    const bool accepted = n <= limit - 2;
    check(body(repeat("(", n) + "0" + repeat(")", n)), accepted);
    check(body(repeat("!", n) + "true"), accepted);
    check(body(repeat("[", n) + "0" + repeat("]", n)), accepted);
    check(body(repeat("f(", n) + "0" + repeat(")", n)), accepted);
    check(body("0" + repeat(" + 0", n)), accepted);
    check(body("x" + repeat(".field", n)), accepted);
    check(body("x" + repeat("[0]", n)), n <= limit - 3);
    check(body("x" + repeat("?", n)), accepted);
    check(body("0" + repeat(" |> f", n)), accepted);
    check("def main() : int { " + repeat("while true { ", n) +
              "return 0" + repeat(" }", n) + " }", accepted);
  }
  for (const auto n : {std::size_t{32}, limit - 1, limit, std::size_t{5000}}) {
    check("def main(x : " + repeat("Box[", n) + "int" + repeat("]", n) +
              ") : int { return 0 }", n < limit);
    check("def main(x : " + repeat("Fn() => ", n) +
              "int) : int { return 0 }", n < limit);
  }
  check(body("x" + repeat("[0]", limit - 3)), true);
  check(body(repeat("x => ", 32) + "0"), true);
  check("def main() : int { " + repeat("if true {} else ", 32) +
            "{ return 0 } }", true);
  check(body("match x { " + repeat("Box(", 32) + "x" +
             repeat(")", 32) + " => 0 }"), true);
  check(body("match x { " + repeat("Box(", 80) + "x" +
             repeat(") as y", 80) + " => 0 }"), false);
  check("def main() : int { " + repeat("if true {} else ", 5000) +
            "{ return 0 } }", false);
  check(body("match x { " + repeat("Box(", 5000) + "x" +
             repeat(")", 5000) + " => 0 }"), false);
  check(body(repeat("x => ", 5000) + "0"), false);
  // Completed subtrees plus iterative growth, across precedence and postfix.
  check(body("(" + repeat("!", 80) + "true)" + repeat(" == true", 80)), false);
  check(body("(" + repeat("f(", 80) + "x" + repeat(")", 80) + ")" +
             repeat(".field", 80)), false);
  check(body(repeat("0 + (", 80) + "0" + repeat(") + 0", 80)), false);
  // Width is not depth; independent siblings and declarations reset the budget.
  check(body("[" + repeat("0,", 5000) + "0]"), true);
  check(repeat("def f() : int { return " + repeat("(", 32) + "0" +
                   repeat(")", 32) + " }\n", 100), true);
  // Failure while completed ASTs are still owned exercises exception cleanup.
  check("def ok() : int { return 1 }\n" +
            body("f(" + repeat("f(", 60) + "0" + repeat(")", 60) + "," +
                 repeat("(", 5000) + "0" + repeat(")", 5000) + ")"), false);
}
