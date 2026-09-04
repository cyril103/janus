#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <filesystem>
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

void expect_rejected(std::string_view source, std::string_view message,
                     std::string_view reason,
                     std::optional<janus::DiagnosticCode> expected_code =
                         std::nullopt) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    expect(false, message);
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(reason) != std::string_view::npos,
           message);
    if (expected_code.has_value())
      expect(error.diagnostic().code == *expected_code,
             std::string{message} + " reports the structured diagnostic code");
  }
}

janus::ast::Program expect_accepted(std::string_view source,
                                    std::string_view message) {
  try {
    janus::frontend::Parser parser{source};
    janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    return program;
  } catch (const janus::CompileError &error) {
    expect(false, std::string{message} + ": " + error.what());
    return {};
  }
}

const llvm::CallInst *find_call(const llvm::Function &function,
                                std::string_view callee) {
  for (const llvm::BasicBlock &block : function)
    for (const llvm::Instruction &instruction : block)
      if (const auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
          call != nullptr && call->getCalledFunction() != nullptr &&
          call->getCalledFunction()->getName() == llvm::StringRef{callee})
        return call;
  return nullptr;
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
tailrec def countDown(value : int, result : int) : int {
    if value == 0 {
        return result
    }
    return countDown(value - 1, result + 1)
}

tailrec def even(value : int) : bool {
    if value == 0 {
        return true
    }
    return odd(value - 1)
}

tailrec def odd(value : int) : bool {
    if value == 0 {
        return false
    }
    return even(value - 1)
}

def throughMatch(value : int) : int {
    return match value {
        0 => 0,
        _ => throughMatch(value - 1)
    }
}

tailrec def branchCount(value : int, down : bool) : int {
    if down {
        return branchCount(value - 1, value > 0)
    } else {
        return branchCount(value + 1, value < 0)
    }
}

def identityText(value : string) : string { return value }

def forwardText(value : string) : string {
    return identityText(value)
}

def observe() : Unit {}

class Counter() {
    borrow tailrec def countDown(value : int, result : int) : int {
        if value == 0 {
            return result
        }
        return this.countDown(value - 1, result + 1)
    }
}

def main() : int {
    val counter : Counter = new Counter()
    val result : int = countDown(100, 0) + counter.countDown(100, 0) + branchCount(1, true)
    delete counter
    return if even(100) { result - 200 + throughMatch(3) } else { 1 }
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "tail_recursion");

  std::string verifier_error;
  llvm::raw_string_ostream verifier_output{verifier_error};
  expect(!llvm::verifyModule(*module, &verifier_output),
         "tail-recursive LLVM IR remains valid");

  const llvm::CallInst *function_call =
      find_call(*module->getFunction("countDown"), "countDown");
  expect(function_call != nullptr && function_call->isMustTailCall(),
         "a terminal recursive function call reuses the current stack frame");

  const llvm::CallInst *mutual_call =
      find_call(*module->getFunction("even"), "odd");
  expect(mutual_call != nullptr && mutual_call->isMustTailCall(),
         "compatible mutual tail recursion also reuses the current frame");

  const llvm::CallInst *match_call =
      find_call(*module->getFunction("throughMatch"), "throughMatch");
  expect(match_call != nullptr && !match_call->isTailCall(),
         "a recursive call below a match-expression PHI is not marked tail");

  const llvm::CallInst *statement_if_call =
      find_call(*module->getFunction("branchCount"), "branchCount");
  expect(statement_if_call != nullptr && statement_if_call->isMustTailCall(),
         "direct returns in statement-if branches remain musttail");

  const llvm::CallInst *method_call = find_call(
      *module->getFunction("Counter__countDown"), "Counter__countDown");
  expect(method_call != nullptr && method_call->isMustTailCall(),
         "a terminal recursive method call reuses the current stack frame");

  const llvm::CallInst *aggregate_return_call =
      find_call(*module->getFunction("forwardText"), "identityText");
  expect(aggregate_return_call != nullptr &&
             !aggregate_return_call->isTailCall(),
         "aggregate returns avoid unsupported target-level tail calls");

  janus::ast::Program unit_program = expect_accepted(R"(
tailrec def finish(remaining : int) : Unit {
    if remaining == 0 { return }
    return finish(remaining - 1)
}
def main() : int { finish(1000000) return 0 }
)", "tailrec recursion returning Unit is accepted");
  llvm::LLVMContext unit_context;
  janus::backend::llvm::IrGenerator unit_generator{unit_context};
  const std::unique_ptr<llvm::Module> unit_module =
      unit_generator.generate(unit_program, "unit_tailrec");
  expect(!llvm::verifyModule(*unit_module),
         "tailrec Unit lowering remains valid LLVM IR");
  const llvm::CallInst *unit_call =
      find_call(*unit_module->getFunction("finish"), "finish");
  expect(unit_call != nullptr && unit_call->isMustTailCall(),
         "terminal Unit recursion is emitted as musttail");

  expect_rejected(R"(
struct Pair(val value : int) {}
tailrec def build(value : int) : Pair {
    if value == 0 { return Pair(0) }
    return build(value - 1)
}
def main() : int { return 0 }
)", "aggregate value returns reject an unverifiable tailrec contract",
                  "musttail",
                  janus::DiagnosticCode::AnalyzerIncompatibleTailrec);

  janus::ast::Program generic_program = expect_accepted(R"(
tailrec def genericDown[T <: Copy](seed : T, value : int) : int {
    if value == 0 { return 0 }
    return genericDown[T](seed, value - 1)
}
def main() : int { return genericDown[int](7, 100) }
)", "a generic tailrec cycle with a provable scalar return is accepted");
  llvm::LLVMContext generic_context;
  janus::backend::llvm::IrGenerator generic_generator{generic_context};
  const std::unique_ptr<llvm::Module> generic_module =
      generic_generator.generate(generic_program, "generic_tailrec");
  expect(!llvm::verifyModule(*generic_module),
         "specialized generic tailrec lowering remains valid LLVM IR");
  const llvm::CallInst *generic_call = find_call(
      *generic_module->getFunction("genericDown__int"), "genericDown__int");
  expect(generic_call != nullptr && generic_call->isMustTailCall(),
         "a generic recursive edge is rechecked as musttail after specialization");

  expect_rejected(R"(
tailrec def genericReturn[T <: Copy](value : T) : T {
    return genericReturn[T](value)
}
def main() : int { return genericReturn[int](1) }
)", "an unprovable generic return ABI rejects tailrec", "musttail",
                  janus::DiagnosticCode::AnalyzerIncompatibleTailrec);

  expect_rejected(R"(
class Resource() {}
tailrec def leak(value : int) : int {
    val resource : Resource = new Resource()
    return leak(value - 1)
}
def main() : int { return 0 }
)", "a live local owner rejects tailrec", "owning value",
                  janus::DiagnosticCode::AnalyzerIncompatibleTailrec);

  expect_rejected(R"(
def missing(value : int) : int {
    if value == 0 { return 0 }
    return missing(value - 1)
}
def main() : int { return missing(2) }
)", "terminal recursion requires the tailrec annotation", "tailrec",
                  janus::DiagnosticCode::AnalyzerTailrecRequired);

  expect_rejected(R"(
class Box(value : int) {}
def make(value : int) : Box {
    if value == 0 { return new Box(0) }
    return make(value - 1)
}
def main() : int { return 0 }
)", "terminal recursion returning a class requires tailrec", "tailrec");

  janus::ast::Program class_return_program = expect_accepted(R"(
class Box(value : int) {}
tailrec def make(value : int) : Box {
    if value == 0 { return new Box(0) }
    return make(value - 1)
}
def main() : int { return 0 }
)", "tailrec recursion returning a class is accepted");
  llvm::LLVMContext class_return_context;
  janus::backend::llvm::IrGenerator class_return_generator{
      class_return_context};
  const std::unique_ptr<llvm::Module> class_return_module =
      class_return_generator.generate(class_return_program, "class_return");
  const llvm::CallInst *class_return_call =
      find_call(*class_return_module->getFunction("make"), "make");
  expect(class_return_call != nullptr && class_return_call->isMustTailCall(),
         "terminal recursion returning a class is emitted as musttail");

  static_cast<void>(expect_accepted(R"(
def factorial(value : int) : int {
    if value <= 1 { return 1 }
    return value * factorial(value - 1)
}
def main() : int { return factorial(5) }
)", "ordinary non-terminal recursion remains legal without tailrec"));

  static_cast<void>(expect_accepted(R"(
def recur(value : int) : int {
    val callback = () => recur(value)
    delete callback
    return value
}
def main() : int { return recur(2) }
)", "recursive calls in expression lambdas do not require tailrec"));

  static_cast<void>(expect_accepted(R"(
def recur(value : int) : int {
    val callback = () => { return recur(value) }
    delete callback
    return value
}
def main() : int { return recur(2) }
)", "recursive calls in block lambdas do not require tailrec"));

  static_cast<void>(expect_accepted(R"(
def mixed(value : int, terminal : bool) : int {
    if value == 0 { return 0 }
    if terminal { return mixed(value - 1, terminal) }
    return 1 + mixed(value - 1, terminal)
}
def main() : int { return mixed(2, true) }
)", "a mixed recursive cycle remains legal without tailrec"));

  expect_rejected(R"(
tailrec def mixed(value : int, terminal : bool) : int {
    if value == 0 { return 0 }
    if terminal { return mixed(value - 1, terminal) }
    return 1 + mixed(value - 1, terminal)
}
def main() : int { return mixed(2, true) }
)", "a mixed recursive cycle rejects tailrec", "terminal");

  janus::ast::Program string_program = expect_accepted(R"(
def repeat(value : string, count : int) : string {
    if count == 0 { return value }
    return repeat(value, count - 1)
}
def main() : int { return 0 }
)", "terminal recursion returning string remains ordinary without tailrec");
  llvm::LLVMContext string_context;
  janus::backend::llvm::IrGenerator string_generator{string_context};
  const std::unique_ptr<llvm::Module> string_module =
      string_generator.generate(string_program, "string_recursion");
  const llvm::CallInst *string_call =
      find_call(*string_module->getFunction("repeat"), "repeat");
  expect(string_call != nullptr && !string_call->isTailCall(),
         "terminal string recursion is not emitted as musttail");

  expect_rejected(R"(
tailrec def lie(value : int) : int { return value }
def main() : int { return lie(2) }
)", "a non-recursive declaration rejects tailrec", "recursion",
                  janus::DiagnosticCode::AnalyzerInvalidTailrec);

  expect_rejected(R"(
tailrec def factorial(value : int) : int {
    if value == 0 { return 0 }
    return value + factorial(value - 1)
}
def main() : int { return factorial(2) }
)", "a non-terminal recursive call rejects tailrec", "terminal",
                  janus::DiagnosticCode::AnalyzerNonTerminalTailrec);

  expect_rejected(R"(
def observe() : Unit {}
tailrec def cleanup(value : int) : int {
    defer observe()
    if value == 0 { return 0 }
    return cleanup(value - 1)
}
def main() : int { return cleanup(2) }
)", "pending defer rejects tailrec", "defer",
                  janus::DiagnosticCode::AnalyzerNonTerminalTailrec);

  expect_rejected(R"(
def observe() : Unit {}
def branchCleanup(value : int) : int {
    if value == 0 {
        defer observe()
        return 0
    }
    return branchCleanup(value - 1)
}
def main() : int { return branchCleanup(2) }
)", "a defer confined to the base branch does not hide guaranteed recursion",
                  "tailrec");

  janus::ast::Program branch_defer_program = expect_accepted(R"(
def observe() : Unit {}
tailrec def branchCleanup(value : int) : int {
    if value == 0 {
        defer observe()
        return 0
    }
    return branchCleanup(value - 1)
}
def main() : int { return branchCleanup(2) }
)", "a defer confined to the base branch permits tailrec");
  llvm::LLVMContext branch_defer_context;
  janus::backend::llvm::IrGenerator branch_defer_generator{
      branch_defer_context};
  const std::unique_ptr<llvm::Module> branch_defer_module =
      branch_defer_generator.generate(branch_defer_program, "branch_defer");
  const llvm::CallInst *branch_defer_call = find_call(
      *branch_defer_module->getFunction("branchCleanup"), "branchCleanup");
  expect(branch_defer_call != nullptr && branch_defer_call->isMustTailCall(),
         "a defer confined to the base branch preserves recursive musttail");

  janus::ast::Program pending_defer_program = expect_accepted(R"(
def observe() : Unit {}
def pendingCleanup(value : int) : int {
    defer observe()
    if value == 0 { return 0 }
    return pendingCleanup(value - 1)
}
def main() : int { return pendingCleanup(2) }
)", "pending defer recursion remains legal without tailrec");
  llvm::LLVMContext pending_defer_context;
  janus::backend::llvm::IrGenerator pending_defer_generator{
      pending_defer_context};
  const std::unique_ptr<llvm::Module> pending_defer_module =
      pending_defer_generator.generate(pending_defer_program, "pending_defer");
  const llvm::CallInst *pending_defer_call = find_call(
      *pending_defer_module->getFunction("pendingCleanup"), "pendingCleanup");
  expect(pending_defer_call != nullptr && !pending_defer_call->isTailCall(),
         "pending defer cleanup prevents recursive musttail emission");

  expect_rejected(R"(
def main() : int {
    return main()
}
)", "recursive top-level main is rejected by the analyzer", "entry ABI",
                  janus::DiagnosticCode::AnalyzerIncompatibleTailrec);

  expect_rejected(R"(
tailrec def main() : int {
    return main()
}
)", "recursive top-level main rejects tailrec", "entry ABI",
                  janus::DiagnosticCode::AnalyzerIncompatibleTailrec);

  expect_rejected(R"(
tailrec def incompatible(value : string) : string {
    if value == "" { return value }
    return incompatible(value)
}
def main() : int { return 0 }
)", "a musttail-incompatible signature rejects tailrec", "musttail",
                  janus::DiagnosticCode::AnalyzerIncompatibleTailrec);

  static_cast<void>(expect_accepted(R"(
def identity(value : int) : int { return value }
def forward(value : int) : int { return identity(value) }
def main() : int { return forward(2) }
)", "a terminal non-recursive call does not require tailrec"));

  static_cast<void>(expect_accepted(R"(
def shadowed(shadowed : (int) => int, value : int) : int {
    return shadowed(value)
}
def main() : int { return shadowed((value : int) => value, 2) }
)", "a homonymous function parameter does not create false recursion"));

  static_cast<void>(expect_accepted(R"(
def conditional(value : int) : int {
    return if value == 0 { 0 } else { conditional(value - 1) }
}
def matching(value : int) : int {
    return match value { 0 => 0, _ => matching(value - 1) }
}
def main() : int { return conditional(2) + matching(2) }
)", "return if/match recursion remains legal without tailrec"));

  expect_rejected(R"(
tailrec def conditional(value : int) : int {
    return if value == 0 { 0 } else { conditional(value - 1) }
}
def main() : int { return conditional(2) }
)", "return-if recursion rejects tailrec", "terminal");

  expect_rejected(R"(
tailrec def matching(value : int) : int {
    return match value { 0 => 0, _ => matching(value - 1) }
}
def main() : int { return matching(2) }
)", "return-match recursion rejects tailrec", "terminal");

  expect_rejected(R"(
tailrec def left(value : int) : int {
    if value == 0 { return 0 }
    return right(value - 1)
}
def right(value : int) : int {
    if value == 0 { return 0 }
    return left(value - 1)
}
def main() : int { return left(2) }
)", "every member of a mutual recursion cycle requires tailrec", "tailrec");

  const std::filesystem::path virtual_root =
      std::filesystem::temp_directory_path() / "janus-tailrec-type-resolution";
  const std::filesystem::path imported_recursion =
      virtual_root / "recursion" / "loops.janus";
  const std::filesystem::path imported_type =
      virtual_root / "aggregate" / "types.janus";
  janus::frontend::ModuleLoader recursion_loader;
  recursion_loader.set_source_override(
      imported_recursion,
      "module recursion.loops\n"
      "tailrec def descend(value : int) : int {\n"
      "  if value == 0 { return 0 }\n"
      "  return descend(value - 1)\n"
      "}\n");
  static_cast<void>(analyzer.analyze(recursion_loader.load(
      virtual_root / "ordinary.janus",
      "import recursion.loops\n"
      "def main() : int { return descend(2) }\n")));
  static_cast<void>(analyzer.analyze(recursion_loader.load(
      virtual_root / "qualified.janus",
      "import recursion.loops as loops\n"
      "def main() : int { return loops.descend(2) }\n")));
  static_cast<void>(analyzer.analyze(recursion_loader.load(
      virtual_root / "renamed.janus",
      "import recursion.loops.{descend as run}\n"
      "def main() : int { return run(2) }\n")));
  expect(true,
         "ordinary, qualified, and renamed imports retain canonical call targets");

  const auto imported_program = [&](bool annotated) {
    janus::frontend::ModuleLoader loader;
    loader.set_source_override(imported_type,
                               "module aggregate.types\nclass Payload() {}\n");
    const std::string source =
        "import aggregate.types.{Payload as Alias}\n" +
        std::string{annotated ? "tailrec " : ""} +
        "def build(value : int) : Alias {\n"
        "  if value == 0 { return new Alias() }\n"
        "  return build(value - 1)\n"
        "}\n"
        "def main() : int { return 0 }\n";
    return loader.load(virtual_root / "main.janus", source);
  };
  try {
    static_cast<void>(analyzer.analyze(imported_program(false)));
    expect(false, "an imported class alias requires tailrec");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find("tailrec") !=
               std::string_view::npos,
           "an imported class alias requires tailrec");
  }
  try {
    static_cast<void>(analyzer.analyze(imported_program(true)));
    expect(true, "an imported class alias accepts tailrec");
  } catch (const janus::CompileError &error) {
    expect(false, std::string{"an imported class alias accepts tailrec: "} +
                      error.what());
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "terminal function and method calls reuse their stack frame\n";
  return 0;
}
