#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <algorithm>
#include <iostream>
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

void expect_valid(std::string_view source, std::string_view message) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
  } catch (const janus::CompileError &error) {
    std::cerr << "FAILED: " << message << ": " << error.what() << '\n';
    ++failures;
  }
}

void expect_error(std::string_view source, std::string_view expected,
                  std::string_view message,
                  std::string_view expected_note = {}) {
  try {
    janus::frontend::Parser parser{source};
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(parser.parse_program()));
    expect(false, message);
  } catch (const janus::CompileError &error) {
    if (std::string_view{error.what()}.find(expected) == std::string_view::npos) {
      std::cerr << "FAILED: " << message << ": expected '" << expected
                << "', got '" << error.what() << "'\n";
      ++failures;
    }
    if (!expected_note.empty()) {
      const auto &notes = error.diagnostic().notes;
      const bool found = std::any_of(
          notes.begin(), notes.end(), [&](const std::string &note) {
            return std::string_view{note}.find(expected_note) !=
                   std::string_view::npos;
          });
      expect(found, "diagnostic reports the transitive pure call chain");
    }
  }
}

} // namespace

int main() {
  expect_valid(R"(
struct Output(val value : int) {}
pure def transform(value : int) : int { return value * 2 }
const def offset() : int { return 1 }
pure def normalize(value : int) : Output {
    var result : int = transform(value)
    result += offset()
    return new Output(result)
}
pure tailrec def even(value : int) : bool {
    if value == 0 { return true }
    return odd(value - 1)
}
pure tailrec def odd(value : int) : bool {
    if value == 0 { return false }
    return even(value - 1)
}
pure def apply(action : pure (int) => int, value : int) : int {
    return action(value)
}
pure def identity[T](value : T) : T { return value }
pure extern def nativeAbs(value : int) : int
class Box(val value : int) {
    pure borrow def read() : int { return value }
}
def main() : int {
    val output : Output = normalize(20)
    val box : Box = new Box(output.value)
    return apply((value : int) => identity[int](value), box.read())
}
)", "pure functions support allocation, local mutation, recursion, callbacks, "
    "generics, trusted FFI and methods");

  expect_valid(R"(
pure def fail(value : int) : int {
    if value < 0 { panic("negative") }
    return value
}
def main() : int { return fail(1) }
)", "deterministic panic is allowed by the pure contract");

  expect_valid(R"(
enum Maybe[T] { Some(T), None }
struct Key(val value : int) derives Hashing, Equality {}
pure def absent() : Maybe[int] { return Maybe.None[int]() }
pure def hashKey(borrow key : Key) : usize {
    return __derivedHash[Key](key)
}
pure def equalKeys(borrow left : Key, borrow right : Key) : bool {
    return __derivedEquals[Key](left, right)
}
pure def asSize(value : int) : usize { return numericCast[usize](value) }
def main() : int {
    val left : Key = new Key(7)
    val right : Key = new Key(7)
    val none : Maybe[int] = absent()
    return if equalKeys(left, right) && hashKey(left) == hashKey(right) &&
              asSize(7) == usize(7) { 0 } else { 1 }
}
)", "qualified enum constructors and deterministic intrinsics are pure");

  expect_error(R"(
var state : int = 1
pure def readState() : int { return state }
def main() : int { return 0 }
)", "cannot observe mutable global 'state'",
               "pure functions reject mutable global reads");

  expect_error(R"(
def io() : int { return 1 }
pure def outer() : int { return nested() }
pure def nested() : int { return io() }
def main() : int { return 0 }
)", "cannot call impure function 'io'",
               "purity is checked transitively", "outer -> nested");

  expect_error(R"(
extern def clock() : int
pure def now() : int { return clock() }
def main() : int { return 0 }
)", "cannot call impure function 'clock'",
               "unannotated FFI is impure");

  expect_error(R"(
class Box(val value : int) { borrow def read() : int { return value } }
pure def inspect(box : Box) : int { return box.read() }
def main() : int { return 0 }
)", "without a pure contract", "borrow methods are not implicitly pure");

  expect_error(R"(
class ImpureBox(val value : int) { borrow def read() : int { return value } }
class PureBox(val value : int) {
    pure borrow def read() : int { return value }
}
pure def inspect(box : ImpureBox) : int { return box.read() }
def main() : int { return 0 }
)", "without a pure contract",
               "purity uses the resolved method rather than its name alone");

  expect_error(R"(
pure def mutate(borrow var value : int) : int { return value }
def main() : int { return 0 }
)", "cannot accept a mutable borrow parameter",
               "pure functions reject mutable borrow parameters");

  expect_error(R"(
pure def apply(action : (int) => int, value : int) : int {
    return action(value)
}
def main() : int { return 0 }
)", "without a pure function contract",
               "callbacks need an explicit pure function type");

  expect_error(R"(
var state : int = 1
def main() : int {
    val action : pure (int) => int = (value : int) => value + state
    return action(1)
}
)", "pure lambda cannot observe mutable global 'state'",
               "pure callback values verify their lambda body");

  expect_error(R"(
def io() : int { return 1 }
def main() : int {
    val action : pure (int) => int = (value : int) => value + io()
    return action(1)
}
)", "pure lambda cannot call impure function 'io'",
               "pure lambdas reject transitive impure calls");

  expect_error(R"(
enum Maybe { None }
class Box() { borrow def None() : int { return 1 } }
pure def inspect(borrow Maybe : Box) : int { return Maybe.None() }
def main() : int { return 0 }
)", "without a pure contract",
               "a local shadowing an enum name is not a pure constructor");

  return failures == 0 ? 0 : 1;
}
