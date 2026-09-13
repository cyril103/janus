#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {
int failures = 0;
const std::string resource = "class Resource(val id : int) {}\n";
void check(std::string_view source, std::string_view error = {}) {
  try {
    const std::string complete =
        std::string{source} + (source.find("def main") == std::string_view::npos
                                   ? "\ndef main() : int { return 0 }"
                                   : "");
    janus::frontend::Parser parser{complete};
    auto program = parser.parse_program();
    auto analysis = janus::semantic::Analyzer{}.analyze(program);
    if (!error.empty()) {
      std::cerr << "Expected error: " << error << "\n" << source << '\n';
      ++failures;
    }
    for (const auto &diagnostic : analysis.diagnostics)
      if (diagnostic.code ==
              janus::DiagnosticCode::AnalyzerPotentialMemoryLeak ||
          diagnostic.code == janus::DiagnosticCode::AnalyzerLoopAllocation) {
        std::cerr << "Unexpected leak warning: " << diagnostic.message << '\n';
        ++failures;
      }
  } catch (const janus::CompileError &actual) {
    if (error.empty() ||
        std::string_view{actual.what()}.find(error) == std::string_view::npos) {
      std::cerr << "Unexpected error: " << actual.what()
                << "\nExpected: " << error << "\n"
                << source << '\n';
      ++failures;
    }
  }
}
void body(std::string_view statements, std::string_view error = {}) {
  check(resource + "def main() : int {\n" + std::string{statements} +
            "\nreturn 0\n}",
        error);
}
} // namespace

int main() {
  body("using val r = new Resource(1)");
  body("using val r : Resource = new Resource(1)\ndelete r");
  body("using val r = new Resource(1)\nusing val s = move r");
  body("using val r = new Resource(1)\nif true { delete r }");
  body("using val r = new Resource(1)\nif true { using val s = move r }");
  body("while false { using val r = new Resource(1) continue }");
  body("using val r = new Resource(1)\nborrow val b = r\ndefer println(b.id)");
  body("using val r = new Resource(1)\nborrow val b = r\nprintln(b.id)\ndelete "
       "r");
  check(
      resource +
      "def make() : Resource { using val r = new Resource(1) return move r }");
  body("using val r = new Resource(1)\nwhile false { delete r }",
       "cannot be deleted from a loop");
  body("using var r = new Resource(1)", "using requires");
  body("using borrow val r = new Resource(1)", "using requires");
  body("using val r : Resource", "expected '='");
  body("using val n = 1", "use val for Copy values");
  body("using val r = new Resource(1)\nr = new Resource(2)", "immutable");
  body("using val r = new Resource(1)\ndelete r\ndelete r",
       "after move or delete");
  body("using val r = new Resource(1)\nusing val s = move r\nprintln(r.id)",
       "after move or delete");
  body("using val r = new Resource(1)\ndefer delete r", "already scheduled");
  body("using val r = new Resource(1)\nborrow val b = r\ndelete "
       "r\nprintln(b.id)",
       "while borrowed");
  body("using val r = new Resource(1)\nborrow val b = r\nusing val s = move "
       "r\nprintln(b.id)",
       "while borrowed");
  body("using val r = new Resource(1)\ndefer println(r.id)\ndelete r",
       "deferred use");
  body("using val r = new Resource(1)\ndefer println(r.id)\nusing val s = move "
       "r",
       "deferred use");
  body("if true { using val r = new Resource(1) }\nprintln(r.id)", "unknown");
  check(resource + "using val r = new Resource(1)", "found 'using'");
  check(resource + "class Holder() { using val r = new Resource(1) }",
        "expected field");
  check(resource + "def f(using val r : Resource) : Unit {}", "found 'using'");
  check(resource +
            "def make() : Resource { using val r = new Resource(1) return r }",
        "scheduled for deferred cleanup");
  check(resource + "def make() : Fn () => int { using val r = new Resource(1) "
                   "return () => r.id }",
        "closure");
  return failures == 0 ? 0 : 1;
}
