#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

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
def countDown(value : int, result : int) : int {
    if value == 0 {
        return result
    }
    return countDown(value - 1, result + 1)
}

def even(value : int) : bool {
    if value == 0 {
        return true
    }
    return odd(value - 1)
}

def odd(value : int) : bool {
    if value == 0 {
        return false
    }
    return even(value - 1)
}

def sum(value : int) : int {
    if value == 0 {
        return 0
    }
    return value + sum(value - 1)
}

def identityText(value : string) : string { return value }

def forwardText(value : string) : string {
    return identityText(value)
}

def observe() : Unit {}

def withDefer(value : int) : int {
    defer observe()
    if value == 0 {
        return value
    }
    return withDefer(value - 1)
}

class Counter() {
    def countDown(value : int, result : int) : int {
        if value == 0 {
            return result
        }
        return this.countDown(value - 1, result + 1)
    }
}

def main() : int {
    val counter : Counter = new Counter()
    val result : int = countDown(100, 0) + counter.countDown(100, 0)
    delete counter
    return if even(100) { result - 200 } else { 1 }
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

  const llvm::CallInst *method_call = find_call(
      *module->getFunction("Counter__countDown"), "Counter__countDown");
  expect(method_call != nullptr && method_call->isMustTailCall(),
         "a terminal recursive method call reuses the current stack frame");

  const llvm::CallInst *non_tail_call =
      find_call(*module->getFunction("sum"), "sum");
  expect(non_tail_call != nullptr && !non_tail_call->isTailCall(),
         "a recursive call followed by arithmetic is not treated as terminal");

  const llvm::CallInst *deferred_call =
      find_call(*module->getFunction("withDefer"), "withDefer");
  expect(deferred_call != nullptr && !deferred_call->isTailCall(),
         "pending deferred work preserves normal recursive return semantics");

  const llvm::CallInst *aggregate_return_call =
      find_call(*module->getFunction("forwardText"), "identityText");
  expect(aggregate_return_call != nullptr &&
             !aggregate_return_call->isTailCall(),
         "aggregate returns avoid unsupported target-level tail calls");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "terminal function and method calls reuse their stack frame\n";
  return 0;
}
