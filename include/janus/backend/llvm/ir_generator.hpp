#pragma once

#include "janus/ast/ast.hpp"

#include <memory>
#include <string_view>

#include <llvm/IR/Module.h>

namespace llvm {
class LLVMContext;
}

namespace janus::backend::llvm {

class IrGenerator final {
public:
  explicit IrGenerator(::llvm::LLVMContext &context) noexcept;

  [[nodiscard]] std::unique_ptr<::llvm::Module>
  generate(const ast::Program &program,
           std::string_view module_name = "janus_module");

private:
  ::llvm::LLVMContext &context_;
};

} // namespace janus::backend::llvm
