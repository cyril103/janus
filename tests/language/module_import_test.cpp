#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
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

void expect_allocas_in_entry_blocks(const llvm::Module &module) {
  for (const llvm::Function &function : module) {
    if (function.empty())
      continue;
    for (const llvm::BasicBlock &block : function)
      for (const llvm::Instruction &instruction : block)
        if (llvm::isa<llvm::AllocaInst>(instruction))
          expect(&block == &function.getEntryBlock(),
                 "stdlib loop stack allocations stay in function entry "
                 "blocks");
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
  expect(syntax.imports.size() == 1 &&
             syntax.imports.front().module_name == "std.array",
         "imports retain their qualified module name");

  janus::frontend::Parser extended_syntax_parser{
      "import std.fs as fs\n"
      "import std.result.{Result, Ok as Success}\n"
      "def main() : int { return 0 }"};
  const janus::ast::Program extended_syntax =
      extended_syntax_parser.parse_program();
  expect(extended_syntax.imports.size() == 2 &&
             extended_syntax.imports[0].module_alias == "fs" &&
             extended_syntax.imports[1].symbols.size() == 2 &&
             extended_syntax.imports[1].symbols[1].alias == "Success",
         "qualified, selective, and renamed imports retain their structure");

  bool empty_selective_rejected = false;
  try {
    janus::frontend::Parser empty_import{
        "import std.fs.{}\ndef main() : int { return 0 }"};
    static_cast<void>(empty_import.parse_program());
  } catch (const janus::CompileError &) {
    empty_selective_rejected = true;
  }
  expect(empty_selective_rejected, "empty selective imports are rejected");

  const std::filesystem::path import_root =
      std::filesystem::temp_directory_path() /
      ("janus-imports-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(import_root / "sample");
  const auto write_source = [](const std::filesystem::path &path,
                               std::string_view source) {
    std::ofstream output{path, std::ios::binary};
    output << source;
  };
  write_source(import_root / "sample" / "api.janus",
               "module sample.api\n"
               "def answer() : int { return 42 }\n"
               "private def hidden() : int { return 0 }\n"
               "struct Box[T](val value : T) {}\n"
               "struct ProjectedBox[T](var value : T) {}\n"
               "enum Status { Ready }\n");
  write_source(import_root / "qualified.janus",
               "import sample.api as api\n"
               "def main() : int { return api.answer() }\n");
  write_source(import_root / "selective.janus",
               "import sample.api.{answer as selected, ProjectedBox}\n"
               "def main() : int {\n"
               "  var box : ProjectedBox[int] = new ProjectedBox[int](1)\n"
               "  borrow var value : int = box.value\n"
               "  value = 2\n"
               "  return selected() + value\n"
               "}\n");
  janus::frontend::ModuleLoader import_loader;
  const janus::ast::Program aliased_import_program =
      import_loader.load(import_root / "qualified.janus");
  janus::semantic::Analyzer import_analyzer;
  static_cast<void>(import_analyzer.analyze(aliased_import_program));
  const janus::ast::Program selective_program =
      import_loader.load(import_root / "selective.janus");
  static_cast<void>(import_analyzer.analyze(selective_program));
  {
    llvm::LLVMContext alias_context;
    janus::backend::llvm::IrGenerator alias_generator{alias_context};
    static_cast<void>(
        alias_generator.generate(selective_program, "selective_import"));
  }
  expect(true, "qualified functions and projected borrows of imported generic "
               "types resolve canonically");

  std::filesystem::create_directories(import_root / "contract");
  write_source(import_root / "contract" / "api.janus",
               "module contract.api\n"
               "trait Requirement {}\n"
               "class Payload() {}\n"
               "trait Contract {\n"
               "  def inspect[T](value : T, payload : Payload) : Payload "
               "where T <: Requirement\n"
               "}\n");
  write_source(import_root / "contract_aliases.janus",
               "import contract.api.{Requirement as R, Payload as P, "
               "Contract as C}\n"
               "class Implementation() extends C {\n"
               "  def inspect[U](value : U, payload : P) : P where U <: R {\n"
               "    return move payload\n"
               "  }\n"
               "}\n"
               "def main() : int { return 0 }\n");
  const janus::ast::Program contract_alias_program =
      import_loader.load(import_root / "contract_aliases.janus");
  static_cast<void>(import_analyzer.analyze(contract_alias_program));
  expect(true, "trait contracts normalize renamed imported types and bounds");

  write_source(import_root / "contract" / "visibility.janus",
               "module contract.visibility\n"
               "trait Named { borrow def name() : string }\n"
               "class Secret(val text : string) extends Named {\n"
               "  internal borrow def name() : string { return text }\n"
               "}\n"
               "def constrained[T <: Named](borrow value : T) : string {\n"
               "  return value.name()\n"
               "}\n");
  write_source(import_root / "trait_visibility_client.janus",
               "import contract.visibility\n"
               "def expose(borrow value : Secret) : string {\n"
               "  val direct : string = value.name()\n"
               "  return constrained[Secret](value)\n"
               "}\n");
  bool public_trait_internal_implementation_rejected = false;
  try {
    static_cast<void>(import_analyzer.analyze(
        import_loader.load(import_root / "trait_visibility_client.janus")));
  } catch (const janus::CompileError &error) {
    public_trait_internal_implementation_rejected =
        std::string_view{error.what()}.find(
            "internal method 'name' cannot implement externally visible "
            "trait method 'Named.name'") != std::string_view::npos;
  }
  expect(public_trait_internal_implementation_rejected,
         "an imported public contract cannot expose its internal "
         "implementation through direct or constrained calls");

  std::filesystem::create_directories(import_root / "secure");
  write_source(import_root / "secure" / "native.janus",
               "module secure.native\n"
               "class Handle internal(private val value : int) {}\n");
  write_source(import_root / "secure" / "factory.janus",
               "module secure.factory\n"
               "import secure.native\n"
               "def make() : Handle { return new Handle(7) }\n");
  write_source(import_root / "internal_constructor_same_namespace.janus",
               "import secure.factory\n"
               "def main() : int { val handle = make() delete handle return 0 }\n");
  static_cast<void>(import_analyzer.analyze(import_loader.load(
      import_root / "internal_constructor_same_namespace.janus")));
  expect(true, "internal constructors remain available within their namespace");

  write_source(import_root / "internal_constructor_external.janus",
               "import secure.native\n"
               "def main() : int { val handle = new Handle(7) delete handle "
               "return 0 }\n");
  bool external_internal_constructor_rejected = false;
  try {
    static_cast<void>(import_analyzer.analyze(import_loader.load(
        import_root / "internal_constructor_external.janus")));
  } catch (const janus::CompileError &error) {
    external_internal_constructor_rejected =
        std::string_view{error.what()}.find(
            "constructor 'Handle' is internal to namespace "
            "'secure'") != std::string_view::npos;
  }
  expect(external_internal_constructor_rejected,
         "internal constructors are rejected outside their namespace");

  write_source(import_root / "not_injected.janus",
               "import sample.api as api\n"
               "def main() : int { return answer() }\n");
  bool qualified_does_not_inject = false;
  try {
    static_cast<void>(import_analyzer.analyze(
        import_loader.load(import_root / "not_injected.janus")));
  } catch (const janus::CompileError &error) {
    qualified_does_not_inject =
        std::string{error.what()}.find("not imported") != std::string::npos;
  }
  expect(qualified_does_not_inject,
         "a qualified import does not inject module symbols");

  write_source(import_root / "private.janus",
               "import sample.api.{hidden}\n"
               "def main() : int { return 0 }\n");
  bool private_selective_rejected = false;
  try {
    static_cast<void>(import_loader.load(import_root / "private.janus"));
  } catch (const janus::CompileError &error) {
    private_selective_rejected =
        std::string{error.what()}.find("private") != std::string::npos;
  }
  expect(private_selective_rejected,
         "selective imports reject private symbols precisely");

  write_source(import_root / "qualified_types.janus",
               "import sample.api as api\n"
               "def main() : int {\n"
               "    val box : api.Box[int] = new api.Box[int](42)\n"
               "    val status : api.Status = api.Status.Ready\n"
               "    return box.value\n"
               "}\n");
  write_source(import_root / "selective_types.janus",
               "import sample.api.{Box as Crate, Status as State}\n"
               "def main() : int {\n"
               "    val box : Crate[int] = new Crate[int](42)\n"
               "    val status : State = State.Ready\n"
               "    return box.value\n"
               "}\n");
  for (const std::string_view source :
       {"qualified_types.janus", "selective_types.janus"}) {
    const janus::ast::Program imported_types =
        import_loader.load(import_root / source);
    static_cast<void>(import_analyzer.analyze(imported_types));
    llvm::LLVMContext imported_type_context;
    janus::backend::llvm::IrGenerator imported_type_generator{
        imported_type_context};
    static_cast<void>(
        imported_type_generator.generate(imported_types, "imported_types"));
  }
  expect(true, "qualified and renamed generic classes and enums preserve "
               "their canonical identity");

  write_source(import_root / "missing.janus",
               "import sample.api.{missing}\n"
               "def main() : int { return 0 }\n");
  bool missing_selective_rejected = false;
  try {
    static_cast<void>(import_loader.load(import_root / "missing.janus"));
  } catch (const janus::CompileError &error) {
    missing_selective_rejected =
        std::string{error.what()}.find("does not exist") != std::string::npos;
  }
  expect(missing_selective_rejected,
         "selective imports reject missing symbols precisely");

  std::filesystem::create_directories(import_root / "other");
  write_source(import_root / "other" / "api.janus",
               "module other.api\ndef answer() : int { return 7 }\n");
  write_source(import_root / "collision.janus",
               "import sample.api.{answer}\n"
               "import other.api.{answer}\n"
               "def main() : int { return answer() }\n");
  bool collision_rejected = false;
  try {
    static_cast<void>(import_loader.load(import_root / "collision.janus"));
  } catch (const janus::CompileError &error) {
    collision_rejected =
        std::string{error.what()}.find("ambiguous") != std::string::npos;
  }
  expect(collision_rejected,
         "distinct selective imports cannot silently collide");

  std::filesystem::create_directories(import_root / "x");
  std::filesystem::create_directories(import_root / "y");
  write_source(import_root / "x" / "api.janus",
               "module x.api\n"
               "def run() : int { return 1 }\n"
               "struct Box(val value : int) {}\n");
  write_source(import_root / "y" / "api.janus",
               "module y.api\n"
               "def run() : int { return 2 }\n"
               "struct Box(val value : int) {}\n");
  write_source(import_root / "a.janus",
               "module a\n"
               "import x.api.{run as execute, Box as Payload}\n"
               "def fromA() : int {\n"
               "    val payload : Payload = new Payload(execute())\n"
               "    return payload.value\n"
               "}\n");
  write_source(import_root / "b.janus",
               "module b\n"
               "import y.api.{run as execute, Box as Payload}\n"
               "def fromB() : int {\n"
               "    val payload : Payload = new Payload(execute())\n"
               "    return payload.value\n"
               "}\n");
  write_source(import_root / "scoped_aliases.janus",
               "import a\n"
               "import b\n"
               "def main() : int { return fromA() + fromB() }\n");
  const janus::ast::Program scoped_alias_program =
      import_loader.load(import_root / "scoped_aliases.janus");
  static_cast<void>(import_analyzer.analyze(scoped_alias_program));
  llvm::LLVMContext scoped_alias_context;
  janus::backend::llvm::IrGenerator scoped_alias_generator{
      scoped_alias_context};
  const std::unique_ptr<llvm::Module> scoped_alias_module =
      scoped_alias_generator.generate(scoped_alias_program, "scoped_aliases");
  std::string scoped_alias_ir;
  llvm::raw_string_ostream scoped_alias_output{scoped_alias_ir};
  scoped_alias_module->print(scoped_alias_output, nullptr);
  scoped_alias_output.flush();
  expect(scoped_alias_ir.find("call i32 @x_api__run") != std::string::npos &&
             scoped_alias_ir.find("call i32 @y_api__run") != std::string::npos,
         "function and type aliases are scoped to their importing modules");

  write_source(import_root / "collision_consumer.janus",
               "module collision_consumer\n"
               "import x.api.{run as execute}\n"
               "import y.api.{run as execute}\n");
  write_source(import_root / "collision_entry.janus",
               "import collision_consumer\n"
               "def main() : int { return 0 }\n");
  bool nested_collision_localized = false;
  try {
    static_cast<void>(
        import_loader.load(import_root / "collision_entry.janus"));
  } catch (const janus::CompileError &error) {
    nested_collision_localized =
        std::string{error.what()}.find("ambiguous") != std::string::npos &&
        error.diagnostic().source_path ==
            std::filesystem::weakly_canonical(import_root /
                                              "collision_consumer.janus");
  }
  expect(nested_collision_localized,
         "same-module alias collisions retain their source path");

  std::filesystem::create_directories(import_root / "leak");
  write_source(import_root / "leak" / "shared.janus",
               "module leak.shared\n"
               "def helper() : int { return 21 }\n");
  write_source(import_root / "leak" / "a.janus",
               "module leak.a\n"
               "import leak.shared\n"
               "class Wrapper() { val value : int = helper() }\n"
               "def fromA() : int {\n"
               "    val wrapper : Wrapper = new Wrapper()\n"
               "    return wrapper.value\n"
               "}\n");
  write_source(import_root / "leak" / "b.janus",
               "module leak.b\n"
               "def fromB() : int { return helper() }\n");
  write_source(import_root / "leak_ab.janus",
               "import leak.a\n"
               "import leak.b\n"
               "def main() : int { return fromA() + fromB() }\n");
  write_source(import_root / "leak_ba.janus",
               "import leak.b\n"
               "import leak.a\n"
               "def main() : int { return fromA() + fromB() }\n");
  for (const std::string_view entry : {"leak_ab.janus", "leak_ba.janus"}) {
    bool sibling_import_rejected = false;
    try {
      janus::frontend::ModuleLoader sibling_loader;
      static_cast<void>(import_analyzer.analyze(
          sibling_loader.load(import_root / entry)));
    } catch (const janus::CompileError &error) {
      sibling_import_rejected =
          std::string{error.what()}.find("not imported") != std::string::npos;
    }
    expect(sibling_import_rejected,
           "a sibling module cannot provide an implicit import");
  }

  write_source(import_root / "leak" / "b.janus",
               "module leak.b\n"
               "import leak.shared\n"
               "def fromB() : int { return helper() }\n");
  janus::frontend::ModuleLoader explicit_sibling_loader;
  static_cast<void>(import_analyzer.analyze(
      explicit_sibling_loader.load(import_root / "leak_ba.janus")));
  expect(true, "an explicit import remains valid in each sibling module");

  write_source(import_root / "leak_qualified.janus",
               "import leak.a as a\n"
               "def main() : int { return a.fromA() }\n");
  janus::frontend::ModuleLoader qualified_sibling_loader;
  static_cast<void>(import_analyzer.analyze(
      qualified_sibling_loader.load(import_root / "leak_qualified.janus")));
  expect(true, "class field initializers retain their declaring module scope");
  std::filesystem::remove_all(import_root);

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
  expect(ir.find("define ptr @collectArray__int") != std::string::npos,
         "collectArray materializes a lazy pipeline without an import cycle");
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
         "collectArray delegates materialization to a generic Builder");
  expect(ir.find("define i64 @IntHashing__hash") != std::string::npos,
         "IntHashing provides a monomorphized primitive hash");
  expect(ir.find("define i1 @IntHashing__equals") != std::string::npos,
         "IntHashing provides primitive equality");
  expect(ir.find("%enum.SetSlot__int = type") != std::string::npos,
         "HashSet represents empty, occupied, and deleted slots inline");
  expect(ir.find("define i64 @HashProbe__next") != std::string::npos,
         "hash collections share a linear probing cursor");
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
  expect(ir.find("define i32 @main(i32 %argc, ptr %argv)") != std::string::npos,
         "the merged program produces its entry point");
  expect_allocas_in_entry_blocks(*module);

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
  expect(adapters_ir.find("%struct.ZipItem__int__int = type") !=
             std::string::npos,
         "zip items are represented as inline records");

  const janus::ast::Program qualified_program =
      loader.load(std::filesystem::path{JANUS_QUALIFIED_ENTRY});
  expect(qualified_program.classes.front().module_name == "qualified.library",
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
  expect(qualified_ir.find("struct.qualified.library.Box") != std::string::npos,
         "a class can be referenced and constructed by qualified name");
  expect(qualified_ir.find("enum.qualified.library.Choice__int") !=
                 std::string::npos &&
             qualified_ir.find("enum.qualified.library.Mode") !=
                 std::string::npos,
         "enum cases can be constructed through qualified type names");

  const janus::ast::Program identity_program =
      loader.load(std::filesystem::path{JANUS_IDENTITY_ENTRY});
  static_cast<void>(analyzer.analyze(identity_program));
  llvm::LLVMContext identity_context;
  janus::backend::llvm::IrGenerator identity_generator{identity_context};
  const std::unique_ptr<llvm::Module> identity_module =
      identity_generator.generate(identity_program, "module_identities");
  std::string identity_ir;
  llvm::raw_string_ostream identity_output{identity_ir};
  identity_module->print(identity_output, nullptr);
  identity_output.flush();
  expect(identity_ir.find("@identity_left__moduleValue") != std::string::npos &&
             identity_ir.find("@identity_right__moduleValue") !=
                 std::string::npos,
         "same-named functions from distinct modules have distinct symbols");
  expect(identity_ir.find("struct.identity.left.Marker") != std::string::npos &&
             identity_ir.find("struct.identity.right.Marker") !=
                 std::string::npos,
         "same-named types from distinct modules retain distinct identities");

  try {
    const janus::ast::Program ambiguous_program =
        loader.load(std::filesystem::path{JANUS_IDENTITY_AMBIGUOUS_ENTRY});
    static_cast<void>(analyzer.analyze(ambiguous_program));
    expect(false, "an ambiguous short type name must be rejected");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "type name 'Marker' is ambiguous") != std::string_view::npos,
           "ambiguous short type names request qualification");
  }
  try {
    const janus::ast::Program ambiguous_function_program = loader.load(
        std::filesystem::path{JANUS_IDENTITY_AMBIGUOUS_FUNCTION_ENTRY});
    static_cast<void>(analyzer.analyze(ambiguous_function_program));
    expect(false, "an ambiguous short function name must be rejected");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "function name 'moduleValue' is ambiguous") !=
               std::string_view::npos,
           "ambiguous short function names request qualification");
  }

  const janus::ast::Program visibility_program =
      loader.load(std::filesystem::path{JANUS_VISIBILITY_ENTRY});
  static_cast<void>(analyzer.analyze(visibility_program));
  llvm::LLVMContext visibility_context;
  janus::backend::llvm::IrGenerator visibility_generator{visibility_context};
  const std::unique_ptr<llvm::Module> visibility_module =
      visibility_generator.generate(visibility_program, "visibility");
  std::string visibility_ir;
  llvm::raw_string_ostream visibility_output{visibility_ir};
  visibility_module->print(visibility_output, nullptr);
  visibility_output.flush();
  expect(visibility_ir.find("define internal i32 @secretValue") !=
             std::string::npos,
         "private top-level functions use internal LLVM linkage");

  try {
    const janus::ast::Program private_function_program = loader.load(
        std::filesystem::path{JANUS_VISIBILITY_PRIVATE_FUNCTION_ENTRY});
    static_cast<void>(analyzer.analyze(private_function_program));
    expect(false, "a private imported function must be inaccessible");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "function 'visibility.library.secretValue' is private") !=
               std::string_view::npos,
           "private function access reports its qualified identity");
  }
  try {
    const janus::ast::Program private_type_program =
        loader.load(std::filesystem::path{JANUS_VISIBILITY_PRIVATE_TYPE_ENTRY});
    static_cast<void>(analyzer.analyze(private_type_program));
    expect(false, "a private imported type must be inaccessible");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "type 'visibility.library.Secret' is private") !=
               std::string_view::npos,
           "private type access reports its qualified identity");
  }
  try {
    const janus::ast::Program internal_method_program = loader.load(
        std::filesystem::path{JANUS_VISIBILITY_INTERNAL_METHOD_ENTRY});
    static_cast<void>(analyzer.analyze(internal_method_program));
    expect(false, "an internal method must be inaccessible outside its module");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "method 'increment' is internal to module "
               "'visibility.library'") != std::string_view::npos,
           "internal method access reports its declaring module");
  }
  try {
    const janus::ast::Program internal_field_program = loader.load(
        std::filesystem::path{JANUS_VISIBILITY_INTERNAL_FIELD_ENTRY});
    static_cast<void>(analyzer.analyze(internal_field_program));
    expect(false, "an internal field must be inaccessible outside its module");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "field 'value' is internal to module "
               "'visibility.library'") != std::string_view::npos,
           "internal field access reports its declaring module");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "qualified imports load the standard library\n";
  return 0;
}
