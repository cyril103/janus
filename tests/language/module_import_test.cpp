#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
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
  const auto has_class = [&](std::string_view name) {
    return std::any_of(program.classes.begin(), program.classes.end(),
                       [&](const janus::ast::ClassDeclaration &declaration) {
                         return declaration.name == name;
                       });
  };
  expect(has_class("Array") && has_class("Iterator") &&
             has_class("ArrayIteratorState"),
         "import std.array loads Array and its iterator support");
  expect(program.enums.size() == 1 && program.enums.front().name == "Option",
         "Array imports Option for its safe operations");
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
  expect(ir.find("define %enum.Option__int @Array__int__getOption") !=
             std::string::npos,
         "Array.getOption returns an optional element");
  expect(ir.find("define %enum.Option__int @Array__int__popOption") !=
             std::string::npos,
         "Array.popOption safely handles an empty array");
  expect(ir.find("define %enum.Option__int @Array__int__find") !=
             std::string::npos,
         "Array.find combines closures with Option");
  expect(ir.find("define i1 @Array__int__isEmpty") != std::string::npos,
         "Array.isEmpty exposes its empty state");
  expect(ir.find("define ptr @Array__int__iterator") != std::string::npos,
         "Array.iterator creates a lazy iterator");
  expect(ir.find("define %enum.Option__int @Iterator__int__next") !=
             std::string::npos,
         "Iterator.next returns Option");
  expect(ir.find("define internal void @Iterator__int__destructor") !=
             std::string::npos,
         "Iterator owns its state closures");
  expect(ir.find("define ptr @Iterator__int__map__int") != std::string::npos,
         "Iterator.map is specialized for its output type");
  expect(
      ir.find("define internal void @MapIteratorState__int__int__destructor") !=
          std::string::npos,
      "a mapped iterator owns its transform and source iterator");
  expect(ir.find("define ptr @Iterator__int__filter") != std::string::npos,
         "Iterator.filter builds a lazy filtering stage");
  expect(ir.find("define ptr @Iterator__int__take") != std::string::npos,
         "Iterator.take limits demand from its source");
  expect(
      ir.find("define internal void @FilterIteratorState__int__destructor") !=
          std::string::npos,
      "a filtered iterator owns its predicate and source");
  expect(ir.find("define internal void @TakeIteratorState__int__destructor") !=
             std::string::npos,
         "a take iterator owns its source");
  expect(ir.find("call void %action.code") != std::string::npos,
         "Array.foreach invokes Unit closures indirectly");
  expect(ir.find("define i32 @main()") != std::string::npos,
         "the merged program produces its entry point");

  const janus::ast::Program algebraic_program =
      loader.load(std::filesystem::path{JANUS_OPTION_RESULT_EXAMPLE});
  expect(algebraic_program.enums.size() == 2,
         "Option and Result are loaded from the standard library");
  static_cast<void>(analyzer.analyze(algebraic_program));
  llvm::LLVMContext algebraic_context;
  janus::backend::llvm::IrGenerator algebraic_generator{algebraic_context};
  const std::unique_ptr<llvm::Module> algebraic_module =
      algebraic_generator.generate(algebraic_program, "option_result");
  std::string algebraic_ir;
  llvm::raw_string_ostream algebraic_output{algebraic_ir};
  algebraic_module->print(algebraic_output, nullptr);
  algebraic_output.flush();
  expect(algebraic_ir.find("%enum.Option__int = type { i32, i32 }") !=
             std::string::npos,
         "the standard Option type is specialized");
  expect(algebraic_ir.find("%enum.Result__int__string = type") !=
             std::string::npos,
         "the standard Result type is specialized");
  expect(algebraic_ir.find("switch i32") != std::string::npos,
         "standard algebraic types can be exhaustively matched");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "qualified imports load the standard library\n";
  return 0;
}
