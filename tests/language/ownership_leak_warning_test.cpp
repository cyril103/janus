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
  return std::any_of(analysis.diagnostics.begin(), analysis.diagnostics.end(),
                     [code](const janus::Diagnostic &diagnostic) {
                       return diagnostic.severity ==
                                  janus::DiagnosticSeverity::Warning &&
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
  expect(!warns_about(propagation, "guard"),
         "the early-exit warning replaces the generic owner warning");

  const auto overwritten = analyze(R"(
class Resource() {}
def main() : int {
    var resource : Resource = new Resource()
    resource = new Resource()
    delete resource
    return 0
}
)");
  expect(warns_with_code(overwritten,
                         janus::DiagnosticCode::AnalyzerOwningValueOverwritten),
         "overwriting a live owner produces a dedicated warning");
  expect(!warns_with_code(overwritten,
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
  expect(warns_with_code(discarded,
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

  const auto must_use = analyze(R"(
enum Option[T] { Some(T), None }
def maybe() : Option[int] { return Option.None[int]() }
def main() : int {
    maybe()
    return 0
}
)");
  expect(
      warns_with_code(must_use, janus::DiagnosticCode::AnalyzerMustUseResult),
      "discarding Option produces a must-use warning");
  expect(!warns_with_code(must_use,
                          janus::DiagnosticCode::AnalyzerOwningResultDiscarded),
         "must-use replaces the generic discarded-owner warning");

  const auto field_overwrite = analyze(R"(
class Resource() {}
class Holder(var resource : Resource) {
    def replace(next : Resource) : Unit {
        resource = move next
    }
    destructor { delete resource }
}
def main() : int { return 0 }
)");
  expect(warns_with_code(field_overwrite,
                         janus::DiagnosticCode::AnalyzerOwningFieldOverwritten),
         "overwriting an owning field produces a dedicated warning");

  const auto incomplete_destructor = analyze(R"(
class Resource() {}
class Owner(private val resource : Resource) {
    destructor {}
}
def main() : int { return 0 }
)");
  expect(warns_with_code(incomplete_destructor,
                         janus::DiagnosticCode::AnalyzerIncompleteDestructor),
         "leaving an owning field alive in a destructor warns");

  const auto missing_destructor = analyze(R"(
class Resource() {}
class Owner(private val resource : Resource) {}
def main() : int { return 0 }
)");
  expect(warns_with_code(missing_destructor,
                         janus::DiagnosticCode::AnalyzerIncompleteDestructor),
         "omitting a destructor for an owning field warns");

  const auto reallocation = analyze(R"(
def main() : int {
    var data : Ptr[int] = alloc[int](usize(1))
    data = realloc[int](data, usize(2))
    free(data)
    return 0
}
)");
  expect(warns_with_code(reallocation,
                         janus::DiagnosticCode::AnalyzerUnsafeReallocation),
         "direct realloc reassignment produces a dedicated warning");
  expect(
      !warns_with_code(reallocation,
                       janus::DiagnosticCode::AnalyzerOwningValueOverwritten),
      "realloc warning replaces the generic overwrite warning");

  expect(warns_with_code(propagation,
                         janus::DiagnosticCode::AnalyzerUnprotectedEarlyExit),
         "question mark identifies an owner lacking deferred cleanup");

  const auto loop_allocation = analyze(R"(
class Resource() {}
def main() : int {
    var index : int = 0
    while index < 1 {
        val resource : Resource = new Resource()
        index = index + 1
    }
    return index
}
)");
  expect(warns_with_code(loop_allocation,
                         janus::DiagnosticCode::AnalyzerLoopAllocation),
         "a live owner inside a loop produces a repeated-leak warning");

  const auto escaping_capture = analyze(R"(
class Resource(val value : int) {}
def makeReader() : () => int {
    val resource : Resource = new Resource(42)
    return () => resource.value
}
def main() : int { return 0 }
)");
  expect(warns_with_code(escaping_capture,
                         janus::DiagnosticCode::AnalyzerEscapingOwningCapture),
         "returning a closure that captures an owner warns");

  const auto pointer_cast = analyze(R"(
def main() : int {
    val pointer : Ptr[int] = alloc[int](usize(1))
    val address : usize = usize(pointer)
    println(address)
    free(pointer)
    return 0
}
)");
  expect(warns_with_code(pointer_cast,
                         janus::DiagnosticCode::AnalyzerAmbiguousPointerCast),
         "pointer-to-integer conversion warns about ownership ambiguity");

  const auto numeric_cast = analyze(R"(
def main() : int {
    val wide : long = long(42)
    val narrow : int = int(wide)
    return narrow
}
)");
  expect(warns_with_code(numeric_cast,
                         janus::DiagnosticCode::AnalyzerLossyNumericCast),
         "narrowing a runtime numeric value warns");

  const auto unused_value = analyze(R"(
def main() : int {
    val unused : int = 42
    return 0
}
)");
  expect(
      warns_with_code(unused_value, janus::DiagnosticCode::AnalyzerUnusedValue),
      "an unread local value warns");

  const auto intentionally_unused = analyze(R"(
def main() : int {
    val _ignored : int = 42
    return 0
}
)");
  expect(intentionally_unused.diagnostics.empty(),
         "an underscore-prefixed unused value is accepted");

  const auto owning_buffer_free = analyze(R"(
class Resource() {}
def main() : int {
    val values : Ptr[Resource] = alloc[Resource](usize(1))
    free(values)
    return 0
}
)");
  expect(warns_with_code(
             owning_buffer_free,
             janus::DiagnosticCode::AnalyzerOwningBufferFreedWithoutCleanup),
         "freeing storage for owning elements warns");

  const auto owning_pointer_store = analyze(R"(
class Resource() {}
def main() : int {
    val values : Ptr[Resource] = alloc[Resource](usize(1))
    values.store(usize(0), new Resource())
    free(values)
    return 0
}
)");
  expect(warns_with_code(
             owning_pointer_store,
             janus::DiagnosticCode::AnalyzerOwningPointerElementOverwritten),
         "storing over a potentially initialized owning element warns");

  const auto owning_buffer_reallocation = analyze(R"(
class Resource() {}
def main() : int {
    val values : Ptr[Resource] = alloc[Resource](usize(1))
    val resized : Ptr[Resource] = realloc[Resource](values, usize(2))
    free(resized)
    return 0
}
)");
  expect(
      warns_with_code(owning_buffer_reallocation,
                      janus::DiagnosticCode::AnalyzerOwningBufferReallocated),
      "reallocating a raw buffer of owners warns");

  const auto borrowed_temporary = analyze(R"(
class Resource() {
    def inspect() : int { return 42 }
}
def main() : int { return new Resource().inspect() }
)");
  expect(warns_with_code(borrowed_temporary,
                         janus::DiagnosticCode::AnalyzerBorrowedTemporaryOwner),
         "borrowing a temporary owner without consuming it warns");

  const auto unprotected_panic = analyze(R"(
class Resource() {}
def main() : int {
    val resource : Resource = new Resource()
    panic("failure")
}
)");
  expect(warns_with_code(unprotected_panic,
                         janus::DiagnosticCode::AnalyzerUnprotectedPanic),
         "panic with a live owner lacking defer warns");
  expect(!warns_about(unprotected_panic, "resource"),
         "the panic warning replaces the generic owner warning");

  const auto extern_ownership = analyze(R"(
extern def nativeUse(data : Ptr[int]) : Unit
def main() : int {
    val data : Ptr[int] = alloc[int](usize(1))
    nativeUse(data)
    free(data)
    return 0
}
)");
  expect(warns_with_code(
             extern_ownership,
             janus::DiagnosticCode::AnalyzerUnannotatedExternOwnership),
         "passing a pointer through an unannotated extern boundary warns");

  const auto ownership_cycle = analyze(R"(
class Node(private val next : Node) {
    destructor { delete next }
}
def main() : int { return 0 }
)");
  expect(
      warns_with_code(ownership_cycle,
                      janus::DiagnosticCode::AnalyzerPotentialOwnershipCycle),
      "a self-referential owning field warns about a potential cycle");

  const auto trivial_pointer_storage = analyze(R"(
def main() : int {
    val values : Ptr[int] = alloc[int](usize(1))
    values.store(usize(0), 42)
    free(values)
    return 0
}
)");
  expect(!warns_with_code(
             trivial_pointer_storage,
             janus::DiagnosticCode::AnalyzerOwningPointerElementOverwritten),
         "raw storage of non-owning elements does not warn");
  expect(!warns_with_code(
             trivial_pointer_storage,
             janus::DiagnosticCode::AnalyzerOwningBufferFreedWithoutCleanup),
         "freeing raw storage of non-owning elements does not warn");

  const auto protected_panic = analyze(R"(
class Resource() {}
def main() : int {
    val resource : Resource = new Resource()
    defer delete resource
    panic("failure")
}
)");
  expect(!warns_with_code(protected_panic,
                          janus::DiagnosticCode::AnalyzerUnprotectedPanic),
         "panic with deferred cleanup does not warn");

  const auto consumed_temporary = analyze(R"(
class Resource() {
    consume def finish() : int {
        delete this
        return 42
    }
}
def main() : int { return new Resource().finish() }
)");
  expect(
      !warns_with_code(consumed_temporary,
                       janus::DiagnosticCode::AnalyzerBorrowedTemporaryOwner),
      "a consuming method safely handles a temporary owner");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "ownership leak warnings follow control flow\n";
  return 0;
}
