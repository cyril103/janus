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
void check_parse(const std::string &source, bool valid,
                 const std::string &message = {}) {
  try {
    static_cast<void>(janus::frontend::Parser{source}.parse_program());
    if (!valid) {
      std::cerr << "unexpected parse acceptance: " << source << '\n';
      ++failures;
    }
  } catch (const janus::CompileError &error) {
    if (valid || (!message.empty() && std::string{error.what()}.find(message) ==
                                          std::string::npos)) {
      std::cerr << "unexpected parse diagnostic: " << error.what() << "\n"
                << source << '\n';
      ++failures;
    }
  }
}
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
  check_parse("def main() : int { val copy = 1 val a = [copy] return 0 }",
              true);
  check_parse("def main() : int { val copy = 1 return [copy][0] }", true);
  check_parse("def main() : int { val copy = 1 return ([copy])[0] }", true);
  check_parse("def main() : int { val mut = 1 return [mut][0] }", true);
  check_parse("def main() : int { val owner = 1 return [move owner][0] }",
              true);
  check_parse("def main() : int { val a = 1 val b = 2 "
              "return [move a, move b][0] }",
              true);
  check_parse("def main() : int { val copy = 1 return [copy: 2][copy] }",
              true);
  check_parse("def main() : int { val copy = 1 return ([copy: 2])[copy] }",
              true);
  check_parse("def main() : int { val mut = 1 val a = [mut] return 0 }", true);
  check_parse("def main() : int { val owner = 1 val a = [move owner] return 0 }",
              true);
  check_parse("def main() : int { val a = 1 val b = 2 "
              "val xs = [move a, move b] return 0 }",
              true);
  check_parse("def main() : int { val copy = 1 val m = [copy: 2] return 0 }",
              true);
  check_parse("def main() : int { val copy = 1 val f = [copy] () => copy }",
              false, "only 'move'");
  check("class R(val n : int) {} def main() : int { "
        "val r = new R(1) val f : Fn (int) => int = "
        "[move r] x => x + r.n val out = f(1) delete f return out }",
        true);
  check_parse("class R(val n : int) {} def main() : int { "
              "val r = new R(1) val f = [move r] ((x) => x + r.n) "
              "return 0 }",
              true);
  check_parse("def main() : int { val mut = 1 val f = [mut] () => mut }",
              false, "only 'move'");
  check_parse("class R(val n : int) {} def main() : int { "
              "val r = new R(1) val f = [move r] [move r] () => r.n }",
              false, "already has an owning capture");
  check_parse("class R(val n : int) {} def main() : int { "
              "val r = new R(1) val f = [move r] ([move absent] () => r.n) }",
              false, "already has an owning capture");
  std::string deeply_stacked =
      "class R(val n : int) {} def main() : int { val r = new R(1) val f = ";
  for (int i = 0; i != 3000; ++i)
    deeply_stacked += "[move r] ";
  deeply_stacked += "() => r.n }";
  check_parse(deeply_stacked, false, "already has an owning capture");
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f : Fn () => int = [move r] () => r.n val a = f() "
        "val b = f() delete f return a + b }", true);
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f : FnOnce () => R = [move r] () => move r val out = f() "
        "delete out return 0 }", true);
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f = [move r] () => r.n return r.n }", false,
        "initialization");
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f = [move r, move r] () => r.n return 0 }", false,
        "exactly one");
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f = [borrow r] () => r.n return 0 }", false,
        "only 'move'");
  check("def main() : int { val f = [move absent] () => 1 return 0 }", false,
        "local owner identifier");
  check("def main() : int { val f = [] () => 1 return 0 }", false,
        "cannot be empty");
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f : FnOnce () => R = [move r] () => move r val out = f() "
        "f() delete out return 0 }", false, "initialization");
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "borrow val view = r val f = [move r] () => r.n delete f "
        "return view.n }", false, "borrowed");
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f = [move r] () => [move r] () => r.n val g = f() "
        "delete g return 0 }", true);
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f = owningCapture[R](r, () => [move r] () => r.n) "
        "val g = f() delete g return 0 }", true);
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f = [move r] () => owningCapture[R](r, () => r.n) "
        "val g = f() delete g return 0 }", true);
  check("class R(val n : int) {} def main() : int { val r = new R(1) "
        "val f = [move r] () => [move r] () => r.n val g = f() "
        "delete f delete g return 0 }", false, "initialization");
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
  check(R"(
def apply[A, B, F <: FnOnce (A) => B](f : F, value : A) : B {
 return f(move value)
}
def main() : int {
 val f : Fn (int) => int = (n : int) => n + 1
 val result = apply(move f, 41)
 return result
})",
        true);
  check(R"(
class Apply() {
 def run[A, B, F <: FnOnce (A) => B](f : F, value : A) : B {
  return f(move value)
 }
}
def main() : int {
 val apply = new Apply()
 defer delete apply
 val f : Fn (int) => int = (n : int) => n + 1
 val result = apply.run(move f, 41)
 return result
})",
        true);
  check(R"(
class Apply[A, B, F <: FnOnce (A) => B](private val callback : F) {
 consume def run(value : A) : B {
  defer delete this
  return callback(move value)
 }
 destructor { delete callback }
}
def main() : int {
 val f : Fn (int) => int = (n : int) => n + 1
 val apply = new Apply(move f)
 return apply.run(41)
})",
        true);
  check(R"(
def increment(value : int) : int { return value + 1 }
def main() : int {
 val callback = increment
 val first = callback(20)
 val second = callback(21)
 delete callback
 return first + second
})",
        true);
  check(R"(
pure def increment(value : int) : int { return value + 1 }
def main() : int {
 val callback : pure Fn (int) => int = increment
 val result = callback(41)
 delete callback
 return result
})",
        true);
  check(R"(
def choose(value : int) : int { return value }
def choose(left : int, right : int) : int { return left + right }
def main() : int { return choose(choose(20), 22) }
)",
        true);
  check(R"(
def choose(callback : Fn () => int) : int { defer delete callback return callback() }
def choose(callback : FnOnce () => int) : int { return callback() }
def main() : int {
 val callback : FnOnce () => int = () => 42
 return choose(move callback)
}
)",
        true);
  check(R"(
pure def apply(borrow var callback : pure FnMut () => int) : int {
 return callback()
}
def main() : int { return 0 }
)",
        true);
  check(R"(
struct Holder(var callback : FnMut () => int) {}
def main() : int {
 var holder = new Holder(() => 42)
 val callback : Fn () => int = owningCapture[Holder](holder, () => {
  borrow var action : FnMut () => int = holder.callback
  return action()
 })
 delete callback
 return 0
}
)",
        false);
  check("def repeat[A](value : A) : A { return move value } "
        "def repeat[B](other : B) : B { return move other } "
        "def main() : int { return 0 }",
        false, "already declared");
  return failures ? 1 : 0;
}
