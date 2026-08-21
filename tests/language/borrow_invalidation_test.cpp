#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

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

void expect_compile_error(std::string_view source,
                          std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
    expect(false, "invalid borrow lifetime must be rejected");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           expected_message);
  }
}

} // namespace

int main() {
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
                       "cannot be mutated while borrowed by 'view'");

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

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "borrow invalidation and escape rules are enforced\n";
  return 0;
}
