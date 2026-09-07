#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void expect_valid(std::string_view source) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
  } catch (const std::exception &error) {
    std::cerr << "FAILED: valid source was rejected: " << error.what() << '\n';
    ++failures;
  }
}

void expect_compile_error(
    std::string_view source, std::string_view expected_message,
    std::optional<janus::DiagnosticCode> expected_code = std::nullopt,
    std::string_view expected_note = {},
    std::string_view expected_related = {}) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
    expect(false, "invalid borrow lifetime must be rejected");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           expected_message);
    expect(error.diagnostic().code != janus::DiagnosticCode::AnalyzerLegacy,
           "borrow lifetime errors use structured diagnostic codes");
    if (expected_code.has_value())
      expect(error.diagnostic().code == *expected_code,
             "borrow error uses its structured diagnostic code");
    if (!expected_note.empty()) {
      const bool found = std::any_of(
          error.diagnostic().notes.begin(), error.diagnostic().notes.end(),
          [&](const std::string &note) {
            return std::string_view{note}.find(expected_note) !=
                   std::string_view::npos;
          });
      expect(found, "borrow diagnostic explains how to end the conflict");
    }
    if (!expected_related.empty()) {
      const bool found =
          std::any_of(error.diagnostic().secondary_locations.begin(),
                      error.diagnostic().secondary_locations.end(),
                      [&](const janus::DiagnosticLocation &location) {
                        return std::string_view{location.label}.find(
                                   expected_related) != std::string_view::npos;
                      });
      expect(found, "borrow diagnostic identifies the retaining last use");
    }
  }
}

} // namespace

int main() {
  expect_valid(R"(
class Box(var value : int) {}
def main() : int {
  val box : Box = new Box(1)
  borrow val view : Box = box
  println(view.value)
  delete box
  return 0
}
)");

  expect_valid(R"(
class Resource(val value : int) {}
def exercise(flag : bool) : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  if flag {
    println(view.value)
  } else {
    delete resource
    return 0
  }
  delete resource
  return 0
}
def main() : int { return exercise(true) }
)");

  expect_compile_error(R"(
class Resource(var value : int) {
  def update(next : int) : Unit { value = next }
}
def exercise(flag : bool) : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  if flag {
    resource.update(2)
  }
  return view.value
}
def main() : int { return exercise(true) }
)",
                       "cannot be mutated while borrowed by 'view'",
                       janus::DiagnosticCode::AnalyzerBorrowInvalidation, {},
                       "last use of 'view'");

  expect_compile_error(R"(
class Resource(val value : int) {}
def exercise() : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  val callback : () => Unit = () => { println(view.value) }
  callback()
  delete resource
  return 0
}
def main() : int { return exercise() }
)",
                       "while borrowed by 'callback'",
                       janus::DiagnosticCode::AnalyzerBorrowInvalidation);

  expect_valid(R"(
class Resource(val value : int) {}
def exercise() : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  val callback : () => Unit = () => { println(view.value) }
  callback()
  delete callback
  delete resource
  return 0
}
def main() : int { return exercise() }
)");

  expect_compile_error(R"(
class Resource(var value : int) {
  def update(next : int) : Unit { value = next }
}
def exercise(run : bool) : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  while run {
    resource.update(2)
    println(view.value)
    break
  }
  delete resource
  return 0
}
def main() : int { return exercise(true) }
)",
                       "cannot be mutated while borrowed by 'view'",
                       janus::DiagnosticCode::AnalyzerBorrowInvalidation);

  expect_valid(R"(
class Resource(var value : int) {
  def update(next : int) : Unit { value = next }
}
def exercise(run : bool) : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  while run {
    println(view.value)
    resource.update(2)
    break
  }
  delete resource
  return 0
}
def main() : int { return exercise(true) }
)");

  expect_valid(R"(
class Resource(val value : int) {}
def exercise(stop : bool) : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  if stop {
    delete resource
    return 0
  }
  val result : int = view.value
  delete resource
  return result
}
def main() : int { return exercise(false) }
)");

  expect_valid(R"(
class Resource(var value : int) {
  def update(next : int) : Unit { value = next }
}
class Observer(private borrow val resource : Resource) {
  borrow def read() : int { return resource.value }
}
def main() : int {
  val resource : Resource = new Resource(1)
  if true {
    val observer : Observer = new Observer(resource)
    println(observer.read())
    delete observer
  }
  resource.update(2)
  var pass : int = 0
  while pass < 1 {
    borrow val view : Resource = resource
    println(view.value)
    pass = pass + 1
  }
  if false {
    borrow val finalView : Resource = resource
    println(finalView.value)
    panic("stopped")
  }
  resource.update(3)
  delete resource
  return 0
}
)");

  expect_compile_error(R"(
class Resource(var value : int) {
  def update(next : int) : Unit { value = next }
}
def main() : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  resource.update(2)
  return view.value
}
)",
                       "cannot be mutated while borrowed by 'view'",
                       janus::DiagnosticCode::AnalyzerBorrowInvalidation,
                       "end the scope of 'view'");

  expect_compile_error(R"(
class Resource(var value : int) {}
class Observer(private borrow val resource : Resource) {}
def main() : int {
  val resource : Resource = new Resource(1)
  val observer : Observer = new Observer(resource)
  borrow var editable : Resource = resource
  delete observer
  return editable.value
}
)",
                       "already borrowed by 'observer'");

  expect_compile_error(R"(
def main() : int {
  val storage : Ptr[int] = alloc[int](usize(1))
  borrow val view : Ptr[int] = storage
  val resized : Ptr[int] = realloc[int](storage, usize(2))
  free(resized)
  return view.load(usize(0))
}
)",
                       "cannot be reallocated while borrowed by 'view'");

  expect_compile_error(R"(
extern def release(consume storage : Ptr[int]) : Unit
def main() : int {
  val storage : Ptr[int] = alloc[int](usize(1))
  borrow val view : Ptr[int] = storage
  release(storage)
  return view.load(usize(0))
}
)",
                       "cannot be consumed by external function 'release' "
                       "while borrowed by 'view'");

  expect_compile_error(R"(
class Resource(val value : int) {}
class Observer(private borrow val resource : Resource) {}
def escape(borrow resource : Resource) : Observer {
  val observer : Observer = new Observer(resource)
  return observer
}
def main() : int { return 0 }
)",
                       "contains a live borrow and cannot escape by return");

  expect_compile_error(
      R"(
class Resource(val value : int) {}
class Observer(private borrow val resource : Resource) {}
def escape(borrow resource : Resource) : Observer {
  return new Observer(resource)
}
def main() : int { return 0 }
)",
      "temporary value of type 'Observer' contains a live borrow");

  expect_compile_error(
      R"(
class Resource(val value : int) {}
class Observer(private borrow val resource : Resource) {}
class Storage(var observer : Observer) {}
def main() : int {
  val resource : Resource = new Resource(1)
  val observer : Observer = new Observer(resource)
  val storage : Storage = new Storage(observer)
  delete storage
  delete observer
  delete resource
  return 0
}
)",
      "contains a live borrow and cannot be stored in owning field 'observer'");

  expect_compile_error(
      R"(
class Resource(val value : int) {}
class Observer(private borrow val resource : Resource) {}
def main() : int {
  val resource : Resource = new Resource(1)
  val observer : Observer = new Observer(resource)
  val copy : Observer = observer
  delete copy
  delete observer
  delete resource
  return 0
}
)",
      "contains a live borrow and cannot be copied into 'copy'");

  expect_compile_error(
      R"(
class Resource(val value : int) {}
def observe(borrow resource : Resource) : Unit { println(resource.value) }
def main() : int {
  val resource : Resource = new Resource(424242)
  borrow val view : Resource = resource
  defer observe(view)
  defer delete resource
  return 0
}
)",
      "invalidate owning value 'resource' before deferred use of borrow 'view'",
      janus::DiagnosticCode::AnalyzerBorrowInvalidation,
      "defer actions execute in LIFO order");

  expect_valid(R"(
class Resource(val value : int) {}
def observe(borrow resource : Resource) : Unit { println(resource.value) }
def main() : int {
  val resource : Resource = new Resource(424242)
  defer delete resource
  borrow val view : Resource = resource
  defer observe(view)
  return 0
}
)");

  expect_valid(R"(
class Resource(var value : int) {
  def update(next : int) : Unit { value = next }
}
def main() : int {
  val resource : Resource = new Resource(1)
  defer delete resource
  defer resource.update(2)
  defer resource.update(3)
  return 0
}
)");

  expect_valid(R"(
enum Option[T] { Some(T), None }
class Resource(val value : int) {}
def observe(borrow resource : Resource) : Unit { println(resource.value) }
def exits(input : Option[int]) : Option[int] {
  var first : bool = true
  while first {
    first = false
    val breakResource : Resource = new Resource(1)
    defer delete breakResource
    borrow val breakView : Resource = breakResource
    defer observe(breakView)
    break
  }
  var second : bool = true
  while second {
    second = false
    val continueResource : Resource = new Resource(2)
    defer delete continueResource
    borrow val continueView : Resource = continueResource
    defer observe(continueView)
    continue
  }
  val tryResource : Resource = new Resource(3)
  defer delete tryResource
  borrow val tryView : Resource = tryResource
  defer observe(tryView)
  val value : int = input?
  return Option.Some[int](value)
}
def panicExit() : int {
  val resource : Resource = new Resource(4)
  defer delete resource
  borrow val view : Resource = resource
  defer observe(view)
  panic("stop")
}
def main() : int {
  val result : Option[int] = exits(Option.Some[int](5))
  return match result { Some(value) => value, None => panicExit() }
}
)");

  expect_compile_error(
      R"(
class Resource(var value : int) {
  def update(next : int) : Unit { value = next }
}
def observe(borrow resource : Resource) : Unit { println(resource.value) }
def main() : int {
  val resource : Resource = new Resource(1)
  defer delete resource
  borrow val view : Resource = resource
  defer observe(view)
  defer resource.update(2)
  return 0
}
)",
      "invalidate owning value 'resource' before deferred use of borrow 'view'",
      janus::DiagnosticCode::AnalyzerBorrowInvalidation);

  expect_valid(R"(
class Resource(var value : int) {
  def update(next : int) : Unit { value = next }
}
def observe(borrow resource : Resource) : Unit { println(resource.value) }
def main() : int {
  val resource : Resource = new Resource(1)
  defer delete resource
  defer resource.update(2)
  borrow val view : Resource = resource
  defer observe(view)
  return 0
}
)");

  expect_compile_error(
      R"(
class Resource(val value : int) {}
def observe(borrow resource : Resource) : Unit { println(resource.value) }
def main() : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  defer observe(view)
  if true {
    defer delete resource
    return 0
  }
  return 1
}
)",
      "invalidate owning value 'resource' before deferred use of borrow 'view'",
      janus::DiagnosticCode::AnalyzerBorrowInvalidation);

  expect_compile_error(
      R"(
class Resource(val value : int) {}
def main() : int {
  val resource : Resource = new Resource(1)
  borrow val view : Resource = resource
  val callback : () => Unit = () => { println(view.value) }
  defer callback()
  defer delete resource
  return 0
}
)",
      "invalidate owning value 'resource' before deferred use of borrow "
      "'callback'",
      janus::DiagnosticCode::AnalyzerBorrowInvalidation);

  expect_valid(R"(
class Resource(val value : int) {}
def main() : int {
  val resource : Resource = new Resource(1)
  defer delete resource
  borrow val view : Resource = resource
  val callback : () => Unit = () => { println(view.value) }
  defer callback()
  return 0
}
)");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "borrow invalidation and escape rules are enforced\n";
  return 0;
}
