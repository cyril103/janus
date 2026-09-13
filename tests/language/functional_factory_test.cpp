#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/semantic/analyzer.hpp"
#include <filesystem>
#include <iostream>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <string>

int main() {
  int failures = 0;
  const std::string caps[]{"Fn", "FnMut", "FnOnce"};
  const auto check = [&](const std::string &body, bool valid,
                         const std::string &diagnostic = {},
                         std::optional<unsigned> allocation_sites =
                             std::nullopt) {
    try {
      janus::frontend::ModuleLoader loader{{JANUS_STDLIB_DIR}};
      auto program = loader.load(
          std::filesystem::temp_directory_path() /
              "functional-factory-test.janus",
          "import std.functional\nimport std.option.{OptionPair}\n"
          "pure def pureInc(value : int) : int { return value + 1 }\n"
          "def ordinaryInc(value : int) : int { return value + 1 }\n" +
              body);
      const auto analysis = janus::semantic::Analyzer{}.analyze(program);
      if (!valid) {
        std::cerr << "unexpected acceptance: " << body << '\n';
        ++failures;
        return;
      }
      llvm::LLVMContext context;
      auto module = janus::backend::llvm::IrGenerator{context}.generate(
          program, "factories");
      if (llvm::verifyModule(*module, &llvm::errs()))
        ++failures;
      if (allocation_sites) {
        unsigned actual = 0;
        for (const auto &function : *module)
          for (const auto &block : function)
            for (const auto &instruction : block)
              if (const auto *call =
                      llvm::dyn_cast<llvm::CallBase>(&instruction))
                if (const auto *callee = call->getCalledFunction();
                    callee && callee->getName() == "janus_alloc")
                  ++actual;
        if (actual != *allocation_sites) {
          std::cerr << "expected " << *allocation_sites
                    << " allocation sites, got " << actual << '\n';
          ++failures;
        }
      }
      for (const auto &[declaration, type] : analysis.local_types) {
        if (!declaration->name.starts_with("pipeline"))
          continue;
        const auto suffix = declaration->name.substr(8);
        const auto expected =
            static_cast<janus::ast::CallCapability>(suffix[0] - '0');
        if (type.call_capability != expected ||
            type.pure_function != (suffix[1] == '1')) {
          std::cerr << "incorrect inferred factory type: " << type.name()
                    << '\n';
          ++failures;
        }
      }
    } catch (const janus::CompileError &error) {
      if (valid ||
          std::string{error.what()}.find(diagnostic) == std::string::npos) {
        std::cerr << "unexpected diagnostic: " << error.what() << "\n"
                  << body << '\n';
        ++failures;
      }
    }
  };
  std::string matrix = "def main() : int {\n";
  unsigned index = 0;
  for (unsigned outer = 0; outer != 3; ++outer)
    for (unsigned inner = 0; inner != 3; ++inner)
      for (unsigned outer_pure = 0; outer_pure != 2; ++outer_pure)
        for (unsigned inner_pure = 0; inner_pure != 2; ++inner_pure) {
          const auto id = std::to_string(index++);
          const auto capability = std::max(outer, inner);
          const auto name = "pipeline" + std::to_string(capability) +
                            std::to_string(outer_pure && inner_pure) + "_" + id;
          matrix += "val outer" + id + " : " + (outer_pure ? "pure " : "") +
                    caps[outer] + " (int) => int = " +
                    (outer_pure ? "pureInc" : "ordinaryInc") + "\n";
          matrix += "val inner" + id + " : " + (inner_pure ? "pure " : "") +
                    caps[inner] + " (int) => int = " +
                    (inner_pure ? "pureInc" : "ordinaryInc") + "\n";
          matrix += "val " + name + " = compose(move outer" + id +
                    ", move inner" + id + ")\n";
          matrix += "println(" + name + "(40))\n";
          if (capability != 2)
            matrix += "println(" + name + "(40)) delete " + name + "\n";
        }
  check(matrix + "return 0 }", true);
  std::string stages;
  std::string uncurried = "def main() : int {\n";
  index = 0;
  for (unsigned outer = 0; outer != 3; ++outer)
    for (unsigned inner = 0; inner != 3; ++inner)
      for (unsigned outer_pure = 0; outer_pure != 2; ++outer_pure)
        for (unsigned inner_pure = 0; inner_pure != 2; ++inner_pure) {
          const auto id = std::to_string(index++);
          const auto inner_type = std::string(inner_pure ? "pure " : "") +
                                  caps[inner] + " (int) => int";
          const auto outer_type = std::string(outer_pure ? "pure " : "") +
                                  caps[outer] + " (int) => " + inner_type;
          stages += std::string(outer_pure ? "pure " : "") + "def stage" + id +
                    "(value : int) : " + inner_type +
                    " { val result : " + inner_type + " = " +
                    (inner_pure ? "pureInc" : "ordinaryInc") +
                    " return move result }\n";
          const auto name = "pipeline" + std::to_string(outer) +
                            std::to_string(outer_pure && inner_pure) + "_" + id;
          uncurried += "val stage" + id + "Value : " + outer_type + " = stage" +
                       id + "\n";
          uncurried +=
              "val " + name + " = uncurry2(move stage" + id + "Value)\n";
          uncurried += "println(" + name + "(new OptionPair(1, 41)))\n";
          if (outer != 2)
            uncurried += "println(" + name +
                         "(new OptionPair(2, 41))) delete " + name + "\n";
        }
  check(stages + uncurried + "return 0 }", true);

  check(R"(
class Resource(val marker : int) { destructor { println(marker) } }
pure def pass(value : Resource) : Resource { return move value }
pure def start(value : int) : pure FnOnce (Resource) => Resource { return pass }
def main() : int {
 val pipeline00_owned = uncurry2(start)
 defer delete pipeline00_owned
 val result = pipeline00_owned(new OptionPair(0, new Resource(42)))
 delete result
 return 0
})",
        true);
  check(R"(
class Resource(val marker : int) {}
def use(value : Resource, count : int) : int { delete value return count }
def main() : int {
 val callback = partialFirst2(use, new Resource(1))
 println(callback(1))
 return callback(2)
})",
        false, "initialization");
  check(R"(
class Resource(val marker : int) {}
def main() : int {
 val resource = new Resource(1)
 val callback = compose(ordinaryInc, (value : int) => value + resource.marker)
 delete callback
 delete resource
 return 0
})",
        false, "borrowed");
  check(R"(
def main() : int {
 val inner : FnOnce (int) => int = ordinaryInc
 val callback : Fn (int) => int = compose(ordinaryInc, move inner)
 delete callback
 return 0
})",
        false, "required");
  check(R"(
def main() : int {
 val callback = pureInc
 val result = callback(41)
 delete callback
 return result
})",
        true, {}, 0);
  check(R"(
pure def add(a : int, b : int) : int { return a + b }
def main() : int {
 val callback = partialFirst2(add, 10)
 val result = callback(32)
 delete callback
 return result
})",
        true, {}, 1);
  return failures ? 1 : 0;
}
