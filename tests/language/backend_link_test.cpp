#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/frontend/parser.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>

int main() {
  janus::frontend::Parser parser{"def main() : int { return 0 }"};
  const janus::ast::Program program = parser.parse_program();
  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "backend_link_test");
  return module == nullptr ? 1 : 0;
}
