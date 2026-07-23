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
  const janus::ast::Program globals_program =
      loader.load(std::filesystem::path{JANUS_GLOBALS_ENTRY});
  expect(globals_program.globals.size() == 5,
         "module loading merges imported and entry globals");
  if (globals_program.globals.size() == 5) {
    expect(globals_program.globals[0].declaration.name == "secret",
           "dependency globals preserve source order");
    expect(globals_program.globals[0].module_name == "global_config",
           "dependency globals preserve their declaring module");
    expect(globals_program.globals[1].declaration.name == "importedCount",
           "all dependency globals are merged");
    expect(globals_program.globals[2].declaration.name == "secret" &&
               globals_program.globals[2].module_name == "other_config",
           "private names may recur in distinct modules");
    expect(globals_program.globals[3].declaration.name == "visibleCount",
           "second dependency globals are merged");
    expect(globals_program.globals[4].declaration.name == "localCount",
           "entry globals follow dependency globals");
    expect(!globals_program.globals[4].module_name.has_value(),
           "entry globals without a module remain unqualified");
  }

  const janus::ast::Program program =
      loader.load(std::filesystem::path{JANUS_ARRAY_EXAMPLE});
  const auto has_class = [&](std::string_view name) {
    return std::any_of(program.classes.begin(), program.classes.end(),
                       [&](const janus::ast::ClassDeclaration &declaration) {
                         return declaration.name == name;
                       });
  };
  expect(has_class("Array") && has_class("Iterator") &&
             has_class("ArrayIteratorState") && has_class("ArrayBuilder"),
         "import std.array loads Array, iterators, and builder support");
  expect(has_class("HashSet") && has_class("SetBuilder") &&
             has_class("HashSetIteratorState"),
         "the standard library loads hash sets, builders, and iterators");
  expect(has_class("HashMap") && has_class("MapBuilder") &&
             has_class("MapEntryIteratorState") &&
             has_class("MapKeyIteratorState") &&
             has_class("MapValueIteratorState"),
         "the standard library loads hash maps and their lazy views");
  expect(std::any_of(program.traits.begin(), program.traits.end(),
                     [](const janus::ast::TraitDeclaration &declaration) {
                       return declaration.name == "Builder";
                     }),
         "the standard library exposes the generic Builder contract");
  expect(std::any_of(program.traits.begin(), program.traits.end(),
                     [](const janus::ast::TraitDeclaration &declaration) {
                       return declaration.name == "Hashing";
                     }),
         "the standard library exposes the generic Hashing strategy");
  expect(std::any_of(program.enums.begin(), program.enums.end(),
                     [](const janus::ast::EnumDeclaration &declaration) {
                       return declaration.name == "Option";
                     }),
         "Array imports Option for its safe operations");
  expect(std::any_of(program.functions.begin(), program.functions.end(),
                     [](const janus::ast::FunctionDeclaration &declaration) {
                       return declaration.name == "main";
                     }),
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
  expect(ir.find("define ptr @Iterator__int__collect") != std::string::npos,
         "Iterator.collect materializes the lazy pipeline");
  expect(ir.find("define void @ArrayBuilder__int__add") != std::string::npos,
         "ArrayBuilder accumulates values incrementally");
  expect(ir.find("define ptr @ArrayBuilder__int__result") != std::string::npos,
         "ArrayBuilder transfers its completed Array");
  expect(ir.find("define i64 @ArrayBuilder__int__size") != std::string::npos,
         "ArrayBuilder reports its accumulated size");
  expect(ir.find("define void @ArrayBuilder__int__clear") != std::string::npos,
         "ArrayBuilder can be reset and reused");
  expect(ir.find("ArrayBuilder__int__addAll__Array__int") != std::string::npos,
         "ArrayBuilder.addAll accepts statically constrained Iterable values");
  expect(ir.find("Iterator__int__collectWith__Array__int__ArrayBuilder__int") !=
             std::string::npos,
         "Iterator.collect delegates materialization to a generic Builder");
  expect(ir.find("define i64 @IntHashing__hash") != std::string::npos,
         "IntHashing provides a monomorphized primitive hash");
  expect(ir.find("define i1 @IntHashing__equals") != std::string::npos,
         "IntHashing provides primitive equality");
  expect(ir.find("%enum.SetSlot__int = type") != std::string::npos,
         "HashSet represents empty, occupied, and deleted slots inline");
  expect(ir.find("define i1 @HashSet__int__IntHashing__add") !=
             std::string::npos,
         "HashSet.add is specialized with its hashing strategy");
  expect(ir.find("define i1 @HashSet__int__IntHashing__remove") !=
             std::string::npos,
         "HashSet supports tombstone-based removal");
  expect(ir.find("define internal void @HashSet__int__IntHashing__resize") !=
             std::string::npos,
         "HashSet grows and rehashes its occupied slots");
  expect(ir.find("define ptr @HashSet__int__IntHashing__iterator") !=
             std::string::npos,
         "HashSet implements Iterable");
  expect(ir.find("define ptr @SetBuilder__int__IntHashing__result") !=
             std::string::npos,
         "SetBuilder transfers ownership of its completed HashSet");
  expect(ir.find("%enum.MapSlot__int__int = type") != std::string::npos,
         "HashMap stores its slot state, keys, and values inline");
  expect(
      ir.find("define %enum.Option__int @HashMap__int__int__IntHashing__put") !=
          std::string::npos,
      "HashMap.put returns the previous optional value");
  expect(ir.find("HashMap__int__int__IntHashing__getOption") !=
             std::string::npos,
         "HashMap provides optional lookup");
  expect(ir.find("HashMap__int__int__IntHashing__remove") != std::string::npos,
         "HashMap removes entries with tombstones");
  expect(ir.find("HashMap__int__int__IntHashing__keys") != std::string::npos &&
             ir.find("HashMap__int__int__IntHashing__values") !=
                 std::string::npos &&
             ir.find("HashMap__int__int__IntHashing__entries") !=
                 std::string::npos,
         "HashMap exposes lazy key, value, and entry iterators");
  expect(ir.find("define ptr @MapBuilder__int__int__IntHashing__result") !=
             std::string::npos,
         "MapBuilder transfers ownership of its completed HashMap");
  expect(ir.find("for.next") != std::string::npos,
         "for loops consume Iterator values");
  expect(ir.find("%for.iterator = call ptr @Array__int__iterator") !=
             std::string::npos,
         "for obtains an iterator directly from Iterable Array values");
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

  const janus::ast::Program range_program =
      loader.load(std::filesystem::path{JANUS_RANGE_EXAMPLE});
  expect(std::any_of(range_program.classes.begin(), range_program.classes.end(),
                     [](const janus::ast::ClassDeclaration &declaration) {
                       return declaration.name == "Range";
                     }),
         "std.range provides the Range iterator state");
  static_cast<void>(analyzer.analyze(range_program));
  llvm::LLVMContext range_context;
  janus::backend::llvm::IrGenerator range_generator{range_context};
  const std::unique_ptr<llvm::Module> range_module =
      range_generator.generate(range_program, "range");
  std::string range_ir;
  llvm::raw_string_ostream range_output{range_ir};
  range_module->print(range_output, nullptr);
  range_output.flush();
  expect(range_ir.find("define ptr @range(i32 %start, i32 %end)") !=
             std::string::npos,
         "range constructs a lazy integer iterator");
  expect(range_ir.find("for.next") != std::string::npos,
         "Range values work with for-in");

  const janus::ast::Program adapters_program =
      loader.load(std::filesystem::path{JANUS_ITERATOR_ADAPTERS_EXAMPLE});
  static_cast<void>(analyzer.analyze(adapters_program));
  llvm::LLVMContext adapters_context;
  janus::backend::llvm::IrGenerator adapters_generator{adapters_context};
  const std::unique_ptr<llvm::Module> adapters_module =
      adapters_generator.generate(adapters_program, "iterator_adapters");
  std::string adapters_ir;
  llvm::raw_string_ostream adapters_output{adapters_ir};
  adapters_module->print(adapters_output, nullptr);
  adapters_output.flush();
  expect(adapters_ir.find("Iterator__int__zip__int") != std::string::npos,
         "zip combines two lazy iterators");
  expect(adapters_ir.find("Iterator__int__enumerate") != std::string::npos,
         "enumerate attaches indices lazily");
  expect(adapters_ir.find("Iterator__int__flatMap__int") != std::string::npos,
         "flatMap switches between lazy inner iterators");
  expect(adapters_ir.find("%enum.ZipItem__int__int = type") !=
             std::string::npos,
         "zip items are represented inline");

  const janus::ast::Program qualified_program =
      loader.load(std::filesystem::path{JANUS_QUALIFIED_ENTRY});
  expect(qualified_program.classes.front().module_name ==
             "qualified.library",
         "type declarations preserve their qualified module identity");
  static_cast<void>(analyzer.analyze(qualified_program));
  llvm::LLVMContext qualified_context;
  janus::backend::llvm::IrGenerator qualified_generator{qualified_context};
  const std::unique_ptr<llvm::Module> qualified_module =
      qualified_generator.generate(qualified_program, "qualified_symbols");
  std::string qualified_ir;
  llvm::raw_string_ostream qualified_output{qualified_ir};
  qualified_module->print(qualified_output, nullptr);
  qualified_output.flush();
  expect(qualified_ir.find("call i32 @answer()") != std::string::npos,
         "a function can be called through its qualified module name");
  expect(qualified_ir.find("struct.qualified.library.Box") !=
             std::string::npos,
         "a class can be referenced and constructed by qualified name");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "qualified imports load the standard library\n";
  return 0;
}
