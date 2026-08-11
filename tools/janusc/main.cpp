#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/diagnostics/renderer.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/semantic/analyzer.hpp"

#include <filesystem>
#include <charconv>
#include <iostream>
#include <string>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

int main(int argc, char **argv) {
  janus::semantic::AnalysisOptions analysis_options;
  std::filesystem::path path;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    auto parse_budget = [&](std::size_t &destination) {
      if (++index == argc)
        return false;
      const std::string_view value{argv[index]};
      const auto [end, error] = std::from_chars(
          value.data(), value.data() + value.size(), destination);
      return error == std::errc{} && end == value.data() + value.size();
    };
    bool valid = true;
    if (argument == "--const-steps")
      valid = parse_budget(analysis_options.constant_step_budget);
    else if (argument == "--const-depth")
      valid = parse_budget(analysis_options.constant_recursion_budget);
    else if (argument == "--const-memory")
      valid = parse_budget(analysis_options.constant_memory_budget);
    else if (argument == "--const-value-size")
      valid = parse_budget(analysis_options.constant_value_size_budget);
    else if (argument.starts_with('-') || !path.empty())
      valid = false;
    else
      path = argument;
    if (!valid) {
      std::cerr << "janusc: invalid constant-evaluation option\n";
      return 2;
    }
  }
  if (path.empty()) {
    std::cerr << "usage: janusc [--const-steps N] [--const-depth N] "
                 "[--const-memory BYTES] [--const-value-size BYTES] "
                 "<source.janus>\n";
    return 2;
  }

  try {
    janus::frontend::ModuleLoader loader{
        {std::filesystem::path{JANUS_STDLIB_DIR}}};
    const janus::ast::Program program = loader.load(path);

    janus::semantic::Analyzer analyzer;
    const janus::semantic::AnalysisResult analysis =
        analyzer.analyze(program, analysis_options);
    if (!analysis.diagnostics.empty())
      std::cerr << janus::diagnostics::render_diagnostics(
          path, {}, analysis.diagnostics,
          janus::diagnostics::DiagnosticFormat::Human);

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
              << ": error: ";
    if (error.diagnostic().code != janus::DiagnosticCode::Unclassified)
      std::cerr << '['
                << janus::diagnostic_code_name(error.diagnostic().code)
                << "] ";
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "janusc: error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
