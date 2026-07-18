#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>
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

} // namespace

int main() {
  janus::frontend::Parser syntax_parser{
      "module application.main\nimport std.array\ndef main() : int { return "
      "0 }"};
  const janus::ast::Program syntax = syntax_parser.parse_program();
  expect(syntax.module_name == "application.main",
         "module declarations use qualified names");
  expect(syntax.imports.size() == 1 && syntax.imports.front() == "std.array",
         "imports retain their qualified module name");

  janus::frontend::ModuleLoader loader{
      {std::filesystem::path{JANUS_STDLIB_DIR}}};
  const janus::ast::Program program =
      loader.load(std::filesystem::path{JANUS_ARRAY_EXAMPLE});
  expect(program.classes.size() == 1 && program.classes.front().name == "Array",
         "import std.array loads the standard Array class");
  expect(program.functions.size() == 1 &&
             program.functions.front().name == "main",
         "the entry module is merged with its dependency");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));
  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "module_import");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("%class.Array__int = type { ptr, i64, i64 }") !=
             std::string::npos,
         "an imported generic class is monomorphized");
  expect(ir.find("%class.Array__double = type { ptr, i64, i64 }") !=
             std::string::npos,
         "Array.map creates a specialization for its output type");
  expect(ir.find("define ptr @Array__int__map__double") != std::string::npos,
         "Array.map is monomorphized with its method type argument");
  expect(ir.find("define i32 @Array__int__fold__int") != std::string::npos,
         "Array.fold is monomorphized with its accumulator type");
  expect(ir.find("call void %action.code") != std::string::npos,
         "Array.foreach invokes Unit closures indirectly");
  expect(ir.find("define i32 @main()") != std::string::npos,
         "the merged program produces its entry point");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "qualified imports load Array from the standard library\n";
  return 0;
}
