#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

janus::semantic::AnalysisResult analyze(std::string_view source) {
  janus::frontend::Parser parser{source};
  return janus::semantic::Analyzer{}.analyze(parser.parse_program());
}

bool warns_about(const janus::semantic::AnalysisResult &analysis,
                 std::string_view name) {
  return std::any_of(
      analysis.diagnostics.begin(), analysis.diagnostics.end(),
      [name](const janus::Diagnostic &diagnostic) {
        return diagnostic.severity == janus::DiagnosticSeverity::Warning &&
               diagnostic.code ==
                   janus::DiagnosticCode::AnalyzerPotentialMemoryLeak &&
               diagnostic.message.find("'" + std::string{name} + "'") !=
                   std::string::npos;
      });
}

bool warns_with_code(const janus::semantic::AnalysisResult &analysis,
                     janus::DiagnosticCode code) {
  return std::any_of(
      analysis.diagnostics.begin(), analysis.diagnostics.end(),
      [code](const janus::Diagnostic &diagnostic) {
        return diagnostic.severity == janus::DiagnosticSeverity::Warning &&
               diagnostic.code == code;
      });
}

} // namespace

int main() {
  const auto simple_leak = analyze(R"(
class Resource() {}
def main() : int {
    val resource : Resource = new Resource()
    return 0
}
)");
  expect(simple_leak.diagnostics.size() == 1,
         "one live owner produces one warning");
  expect(warns_about(simple_leak, "resource"),
         "the warning identifies the owning local");
  expect(simple_leak.diagnostics.front().primary_location.line == 4,
         "the warning points at the owning declaration");

  const auto cleaned = analyze(R"(
class Resource() {}
def dispose(resource : Resource) : Unit { delete resource }
def make() : Resource {
    val returned : Resource = new Resource()
    return returned
}
def main() : int {
    val deferred : Resource = new Resource()
    defer delete deferred
    val deleted : Resource = new Resource()
    delete deleted
    val transferred : Resource = make()
    dispose(move transferred)
    return 0
}
)");
  expect(cleaned.diagnostics.empty(),
         "deleted, deferred, moved, and returned owners do not warn");

  const auto conditional = analyze(R"(
class Resource() {}
def maybeDelete(condition : bool) : Unit {
    val conditional : Resource = new Resource()
    if condition {
        delete conditional
    }
}
def main() : int {
    maybeDelete(true)
    return 0
}
)");
  expect(warns_about(conditional, "conditional"),
         "an owner surviving one conditional path produces a warning");

  const auto all_branches = analyze(R"(
class Resource() {}
def alwaysDelete(condition : bool) : Unit {
    val resource : Resource = new Resource()
    if condition {
        delete resource
    } else {
        delete resource
    }
}
def main() : int {
    alwaysDelete(true)
    return 0
}
)");
  expect(all_branches.diagnostics.empty(),
         "an owner deleted on every conditional path does not warn");

  const auto deferred_branches = analyze(R"(
class Resource() {}
def alwaysDefer(condition : bool) : Unit {
    val resource : Resource = new Resource()
    if condition {
        defer delete resource
    } else {
        defer delete resource
    }
}
def main() : int {
    alwaysDefer(true)
    return 0
}
)");
  expect(deferred_branches.diagnostics.empty(),
         "an owner deferred on every conditional path does not warn");

  const auto propagation = analyze(R"(
enum Option[T] { Some(T), None }
class Resource() {}
def attempt(value : Option[int]) : Option[int] {
    val guard : Resource = new Resource()
    val item : int = value?
    delete guard
    return Option.Some[int](item)
}
def main() : int { return 0 }
)");
  expect(warns_about(propagation, "guard"),
         "an owner live across a propagating question mark warns");

  const auto overwritten = analyze(R"(
class Resource() {}
def main() : int {
    var resource : Resource = new Resource()
    resource = new Resource()
    delete resource
    return 0
}
)");
  expect(warns_with_code(
             overwritten,
             janus::DiagnosticCode::AnalyzerOwningValueOverwritten),
         "overwriting a live owner produces a dedicated warning");
  expect(!warns_with_code(
             overwritten,
             janus::DiagnosticCode::AnalyzerPotentialMemoryLeak),
         "cleaning the replacement avoids a scope-exit leak warning");

  const auto consumed_before_assignment = analyze(R"(
class Resource() {}
def renew(resource : Resource) : Resource {
    delete resource
    return new Resource()
}
def main() : int {
    var resource : Resource = new Resource()
    resource = renew(move resource)
    delete resource
    return 0
}
)");
  expect(consumed_before_assignment.diagnostics.empty(),
         "an assignment whose expression consumes the old owner does not warn");

  const auto discarded = analyze(R"(
class Resource() {}
def make() : Resource { return new Resource() }
def main() : int {
    make()
    return 0
}
)");
  expect(warns_with_code(
             discarded,
             janus::DiagnosticCode::AnalyzerOwningResultDiscarded),
         "discarding an owning call result produces a dedicated warning");

  const auto unit_result = analyze(R"(
class Resource() {}
def dispose(resource : Resource) : Unit { delete resource }
def main() : int {
    dispose(new Resource())
    return 0
}
)");
  expect(unit_result.diagnostics.empty(),
         "discarding a Unit call result does not warn");

  const auto freed_pointers = analyze(R"(
def main() : int {
    val direct : Ptr[int] = alloc[int](usize(1))
    free(direct)
    val deferred : Ptr[int] = alloc[int](usize(1))
    defer free(deferred)
    val missing : Ptr[int] = null[int]()
    if missing != null[int]() { return 1 }
    null[int]()
    return 0
}
)");
  expect(freed_pointers.diagnostics.empty(),
         "free consumes pointers and discarding null does not warn");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "ownership leak warnings follow control flow\n";
  return 0;
}
