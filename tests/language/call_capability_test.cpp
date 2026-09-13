#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"
#include <iostream>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <string>

namespace {
int failures = 0;
void check(const std::string &source, bool valid,
           const std::string &message = {}) {
  try {
    auto program = janus::frontend::Parser{source}.parse_program();
    const auto analysis = janus::semantic::Analyzer{}.analyze(program);
    if (valid) {
      llvm::LLVMContext context;
      auto module = janus::backend::llvm::IrGenerator{context}.generate(
          program, "capabilities");
      if (llvm::verifyModule(*module, &llvm::errs()))
        ++failures;
    }
    if (!valid) {
      std::cerr << "unexpected acceptance: " << source << '\n';
      ++failures;
    }
  } catch (const janus::CompileError &error) {
    if (valid || (!message.empty() && std::string{error.what()}.find(message) ==
                                          std::string::npos)) {
      std::cerr << "unexpected diagnostic: " << error.what() << "\n"
                << source << '\n';
      ++failures;
    }
  }
}
} // namespace
int main() {
  const std::string names[]{"Fn", "FnMut", "FnOnce"};
  for (int source = 0; source != 3; ++source)
    for (int target = 0; target != 3; ++target)
      check("def take(f : " + names[target] +
                " () => int) : Unit { delete f } "
                "def main() : int { val f : " +
                names[source] +
                " () => int = () => 1 "
                "take(move f) return 0 }",
            source <= target);
  check("def take(f : () => int) : Unit {} def main() : int { return 0 }",
        false, "explicit Fn");
  check("def main() : int { val f : FnOnce () => int = () => 1 val a = f() "
        "return f() }",
        false, "initialization");
  check("def main() : int { val f : FnOnce () => int = () => 1 while true { "
        "println(f()) } return 0 }",
        false, "loop");
  check("def main() : int { while false { val f : FnOnce () => int = () => 1 "
        "println(f()) } return 0 }",
        true);
  check("def read(borrow f : Fn () => int) : int { return f() + f() } def "
        "main() : int { return 0 }",
        true);
  check("def read(borrow f : FnMut () => int) : int { return f() } def main() "
        ": int { return 0 }",
        false, "exclusive");
  check("def read(borrow var f : FnOnce () => int) : int { return f() } def "
        "main() : int { return 0 }",
        false, "ownership");
  check("def main() : int { var n = 0 val f : Fn () => Unit = () => { n = n + "
        "1 } delete f return 0 }",
        false);
  check("def main() : int { var n = 0 val f : FnMut () => Unit = () => { n = n "
        "+ 1 } f() f() delete f return 0 }",
        true);
  check("def main() : int { val f : Fn () => int = () => { var n = 0 n = n + 1 "
        "return n } delete f return 0 }",
        true);
  check("def main() : int { val f : pure Fn () => int = () => 1 val g : Fn () "
        "=> int = move f delete g return 0 }",
        false);
  check("def main() : int { val f : FnOnce () => int = () => 1 defer delete f "
        "return f() }",
        true);
  check("def main() : int { val f : FnOnce () => Unit = () => println(1) defer "
        "f() f() return 0 }",
        false, "deferred");
  check(R"(
class Resource(val value : int) {}
def factory[T](value : T) : FnOnce () => T {
 return owningCapture[T](value, () => move value)
}
def main() : int {
 val f = factory[Resource](new Resource(42))
 val r = f()
 delete r
 return 0
})",
        true);
  check(R"(
def apply[T, F <: FnOnce () => T](f : F) : T { return f() }
def main() : int {
 val f : FnOnce () => int = () => 42
 return apply[int, FnOnce () => int](move f)
})",
        true);
  check(R"(
class Holder[F <: FnMut () => int](private val callback : F) {
 def call() : int { return callback() }
 destructor { delete callback }
}
def main() : int {
 val holder = new Holder[Fn () => int](() => 42)
 val result = holder.call()
 delete holder
 return result
})",
        true);
  check(R"(
def take[F <: FnOnce () => int](callback : F) : int { return callback() }
def forward[F <: Fn () => int](callback : F) : int { return take[F](move callback) }
def main() : int { return forward[Fn () => int](() => 42) }
)",
        true);
  check("class Holder(val f : FnOnce () => Unit) { def call() : Unit { f() } } "
        "def main() : int { return 0 }",
        false, "consume method");
  check("class Resource() { def close() : Unit { delete this } } def main() : "
        "int { return 0 }",
        false, "consume method");
  check("def main() : int { val f : FnOnce () => int = () => 1 if true { "
        "println(f()) } return f() }",
        false);
  check("def use(f : FnOnce () => int, condition : bool) : int { if condition "
        "{ return f() } return f() } def main() : int { return 0 }",
        true);
  check("def main() : int { var f : FnOnce () => int = () => 1 println(f()) f "
        "= () => 2 return f() }",
        true);
  check("class R(val n : int) {} def make() : FnOnce () => int { val r = new "
        "R(1) return () => r.n } def main() : int { return 0 }",
        false, "borrowed value");
  check("class R(val n : int) { destructor { println(n) } } def main() : int { "
        "val r = new R(1) val f : pure FnOnce () => int = owningCapture[R](r, "
        "() => r.n) return f() }",
        false, "pure");
  check("class R(val n : int) {} def main() : int { val r = new R(1) val f : "
        "pure FnOnce () => int = owningCapture[R](r, () => r.n) return f() }",
        true);
  check("class R(val n : int) { destructor { println(n) } } def main() : int { "
        "val f : pure Fn () => Unit = () => { val r = new R(1) delete r } "
        "delete f return 0 }",
        false, "pure");
  check("class R(val n : int) { def capture() : FnOnce () => int { "
        "return owningCapture[R](this, () => this.n) } } "
        "def main() : int { return 0 }",
        false, "consume method");
  for (const std::string cap : {"Fn", "FnMut"}) {
    check("def main() : int { val p = alloc[int](usize(1)) val f : " + cap +
              " () => Unit = owningCapture[Ptr[int]](p, () => "
              "p.store(usize(0), 1)) "
              "f() delete f return 0 }",
          cap == "FnMut");
    check("def change(borrow var n : int) : Unit { n = n + 1 } "
          "def main() : int { var n = 0 val f : " +
              cap + " () => Unit = () => change(n) f() delete f return 0 }",
          cap == "FnMut");
  }
  check("def main() : int { val p = alloc[int](usize(1)) val f : FnOnce () "
        "=> Unit = owningCapture[Ptr[int]](p, () => free(p)) f() return 0 }",
        true);
  check("def inspect(borrow n : int) : Unit { val f : Fn (borrow var int) "
        "=> Unit = (borrow var v) => { v = v + 1 } f(n) delete f } "
        "def main() : int { return 0 }",
        false, "shared borrow");
  for (const std::string cap : {"Fn", "FnMut"})
    check("def main() : int { var n = 0 val change : Fn (borrow var int) "
          "=> Unit = (borrow var v) => { v = v + 1 } val f : " +
              cap +
              " () => Unit = () => change(n) f() delete f delete change return "
              "0 }",
          cap == "FnMut");
  return failures ? 1 : 0;
}
