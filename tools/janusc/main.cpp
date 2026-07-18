#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/semantic/analyzer.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: janusc <source.janus>\n";
    return 2;
  }

  const std::filesystem::path path = argv[1];

  try {
    janus::frontend::ModuleLoader loader{
        {std::filesystem::path{JANUS_STDLIB_DIR}}};
    const janus::ast::Program program = loader.load(path);

    janus::semantic::Analyzer analyzer;
    [[maybe_unused]] const janus::semantic::AnalysisResult analysis =
        analyzer.analyze(program);

    llvm::LLVMContext context;
    janus::backend::llvm::IrGenerator generator{context};
    std::unique_ptr<llvm::Module> module =
        generator.generate(program, path.string());

    if (llvm::verifyModule(*module, &llvm::errs())) {
      std::cerr << "janusc: generated invalid LLVM IR\n";
      return 1;
    }

    module->print(llvm::outs(), nullptr);
  } catch (const janus::CompileError &error) {
    const janus::SourceLocation location = error.location();
    std::cerr << path.string() << ':' << location.line << ':' << location.column
              << ": error: " << error.what() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "janusc: error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
