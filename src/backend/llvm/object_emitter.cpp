#include "janus/backend/llvm/object_emitter.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>

namespace janus::backend::llvm {

void emit_object(::llvm::Module &module, const std::filesystem::path &output,
                 bool optimize) {
  static std::once_flag initialization;
  std::call_once(initialization, [] {
    if (::llvm::InitializeNativeTarget() ||
        ::llvm::InitializeNativeTargetAsmPrinter())
      throw std::runtime_error{"cannot initialize the native LLVM target"};
  });

  const ::llvm::Triple triple{module.getTargetTriple()};
  std::string lookup_error;
  const ::llvm::Target *target =
      ::llvm::TargetRegistry::lookupTarget(triple, lookup_error);
  if (target == nullptr)
    throw std::runtime_error{"cannot select LLVM target '" +
                             triple.str() + "': " + lookup_error};

  ::llvm::TargetOptions options;
  std::unique_ptr<::llvm::TargetMachine> machine{
      target->createTargetMachine(
          triple, "generic", "", options, ::llvm::Reloc::PIC_,
          std::nullopt,
          optimize ? ::llvm::CodeGenOptLevel::Aggressive
                   : ::llvm::CodeGenOptLevel::None)};
  if (machine == nullptr)
    throw std::runtime_error{"cannot create the native LLVM target machine"};
  module.setDataLayout(machine->createDataLayout());

  std::error_code error;
  ::llvm::raw_fd_ostream stream{output.string(), error,
                                ::llvm::sys::fs::OF_None};
  if (error)
    throw std::runtime_error{"cannot create object file '" + output.string() +
                             "': " + error.message()};

  ::llvm::legacy::PassManager passes;
  if (machine->addPassesToEmitFile(
          passes, stream, nullptr, ::llvm::CodeGenFileType::ObjectFile))
    throw std::runtime_error{"LLVM target cannot emit object files"};
  passes.run(module);
  stream.flush();
}

} // namespace janus::backend::llvm
