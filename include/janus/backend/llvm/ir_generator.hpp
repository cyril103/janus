#pragma once

#include "janus/ast/ast.hpp"

#include <memory>
#include <string_view>

#include <llvm/IR/Module.h>

namespace llvm {
class LLVMContext;
}

namespace janus::backend::llvm {

enum class PanicTraceMode : unsigned {
  Off,
  Short,
  Full,
};

class IrGenerator final {
public:
  explicit IrGenerator(::llvm::LLVMContext &context) noexcept;

  [[nodiscard]] std::unique_ptr<::llvm::Module>
  generate(const ast::Program &program,
           std::string_view module_name = "janus_module",
           PanicTraceMode panic_trace = PanicTraceMode::Full,
           bool dependencies_only = false);

private:
  ::llvm::LLVMContext &context_;
};

} // namespace janus::backend::llvm
