#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void expect_compile_error(std::string_view source,
                          std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    expect(false, "invalid global declaration must fail");
  } catch (const janus::CompileError &error) {
    if (std::string_view{error.what()}.find(expected_message) ==
        std::string_view::npos) {
      std::cerr << "FAILED: expected diagnostic containing '" << expected_message
                << "', got '" << error.what() << "'\n";
      ++failures;
    }
  }
}

} // namespace

int main() {
  janus::frontend::Parser parser{R"(
val answer : int = 42
var requests : int = 0
private val enabled : bool = !false
val greeting : string = "Bonjour"

def globalAnswer() : int {
    return answer
}

def reader() : () => int {
    return () => answer
}

def main() : int {
    requests = 2
    val answer : int = 7
    return answer
}
)"};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);

  expect(analysis.globals.size() == 4,
         "semantic analysis exposes four global symbols");
  expect(!analysis.globals.at("answer").is_mutable,
         "global val is immutable");
  expect(analysis.globals.at("requests").is_mutable,
         "global var is mutable");
  expect(analysis.globals.at("requests").type.concrete->kind() ==
             janus::TypeKind::Int,
         "global var keeps its declared type");
  expect(analysis.functions.at("main").at("answer").type.concrete->kind() ==
             janus::TypeKind::Int,
         "a local value may shadow a global");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "global_variables");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("@__janus_global_entry__answer = constant i32 42") !=
             std::string::npos,
         "global val is emitted as an LLVM constant");
  expect(ir.find("@__janus_global_entry__requests = global i32 0") !=
             std::string::npos,
         "global var is emitted as writable LLVM storage");
  expect(ir.find("@__janus_global_entry__enabled = internal constant i1 true") !=
             std::string::npos,
         "private global uses internal linkage");
  expect(ir.find("@__janus_global_entry__greeting = constant { ptr, i64 }") !=
             std::string::npos,
         "global string stores static data and its length");
  expect(ir.find("load i32, ptr @__janus_global_entry__answer") !=
             std::string::npos,
         "global reads load from global storage");
  expect(ir.find("store i32 2, ptr @__janus_global_entry__requests") !=
             std::string::npos,
         "global assignments store into global storage");
  expect(ir.find("call ptr @janus_alloc") == std::string::npos,
         "a lambda does not capture global storage");

  expect_compile_error(
      "var pending : int\ndef main() : int { return 0 }",
      "global variable 'pending' requires an initializer");
  expect_compile_error(
      "val wrong : bool = 1\ndef main() : int { return 0 }",
      "constant expression of type 'int' cannot initialize type 'bool'");
  expect_compile_error(
      "val unit : Unit = println(\"x\")\ndef main() : int { return 0 }",
      "Unit cannot be used as a global value type");
  expect_compile_error(
      "val answer : int = 1\nval answer : int = 2\n"
      "def main() : int { return answer }",
      "global value 'answer' is already declared");
  expect_compile_error(
      "val answer : int = 1\n"
      "def main() : int { answer = 2 return answer }",
      "cannot assign to immutable global value 'answer'");

  janus::frontend::Parser constant_parser{R"(
val minute : int = 60
val hour : int = minute * 60
val small : byte = 120 + 7
val ready : bool = hour == 3600 && !false
def main() : int { return hour }
)"};
  const janus::ast::Program constant_program =
      constant_parser.parse_program();
  static_cast<void>(analyzer.analyze(constant_program));
  llvm::LLVMContext constant_context;
  janus::backend::llvm::IrGenerator constant_generator{constant_context};
  const std::unique_ptr<llvm::Module> constant_module =
      constant_generator.generate(constant_program, "constant_globals");
  std::string constant_ir;
  llvm::raw_string_ostream constant_output{constant_ir};
  constant_module->print(constant_output, nullptr);
  constant_output.flush();
  expect(constant_ir.find(
             "@__janus_global_entry__hour = constant i32 3600") !=
             std::string::npos,
         "constant global references and arithmetic are folded");
  expect(constant_ir.find(
             "@__janus_global_entry__small = constant i8 127") !=
             std::string::npos,
         "constant arithmetic uses the declared integer type");
  expect(constant_ir.find(
             "@__janus_global_entry__ready = constant i1 true") !=
             std::string::npos,
         "constant comparisons and logical operators are folded");

  expect_compile_error(
      "val first : int = second\nval second : int = first\n"
      "def main() : int { return 0 }",
      "cyclic global constant dependency");
  expect_compile_error(
      "val invalid : int = 1 / 0\n"
      "def main() : int { return 0 }",
      "division by zero in constant expression");
  expect_compile_error(
      "val invalid : byte = 127 + 1\n"
      "def main() : int { return 0 }",
      "constant integer expression overflows type 'byte'");
  expect_compile_error(
      "var mutable : int = 1\nval copy : int = mutable\n"
      "def main() : int { return 0 }",
      "cannot depend on mutable global 'mutable'");

  janus::frontend::Parser dynamic_parser{R"(
val dynamic : int = compute()
def compute() : int { return 21 * 2 }
def main() : int { return dynamic }
)"};
  const janus::ast::Program dynamic_program = dynamic_parser.parse_program();
  static_cast<void>(analyzer.analyze(dynamic_program));
  llvm::LLVMContext dynamic_context;
  janus::backend::llvm::IrGenerator dynamic_generator{dynamic_context};
  const std::unique_ptr<llvm::Module> dynamic_module =
      dynamic_generator.generate(dynamic_program, "dynamic_globals");
  std::string dynamic_ir;
  llvm::raw_string_ostream dynamic_output{dynamic_ir};
  dynamic_module->print(dynamic_output, nullptr);
  dynamic_output.flush();
  expect(dynamic_ir.find(
             "@__janus_global_entry__dynamic = global i32 0") !=
             std::string::npos,
         "dynamic global starts with zeroed native storage");
  expect(dynamic_ir.find("define internal void @__janus_init_globals()") !=
             std::string::npos,
         "dynamic globals generate an initialization function");
  expect(dynamic_ir.find("call i32 @compute()") != std::string::npos &&
             dynamic_ir.find(
                 "store i32 %compute.result, ptr "
                 "@__janus_global_entry__dynamic") != std::string::npos,
         "dynamic initializer result is stored globally");
  expect(dynamic_ir.find("call void @__janus_init_globals()") !=
             std::string::npos,
         "entry point invokes global initialization");
  expect_compile_error(
      "val invalid : bool = compute()\n"
      "def compute() : int { return 1 }\n"
      "def main() : int { return 0 }",
      "cannot use expression of type 'int' where type 'bool' is required");

  janus::frontend::Parser dependency_parser{R"(
val derived : int = base + 1
val base : int = compute()
def compute() : int { return 41 }
def main() : int { return derived }
)"};
  const janus::ast::Program dependency_program =
      dependency_parser.parse_program();
  static_cast<void>(analyzer.analyze(dependency_program));
  llvm::LLVMContext dependency_context;
  janus::backend::llvm::IrGenerator dependency_generator{dependency_context};
  const std::unique_ptr<llvm::Module> dependency_module =
      dependency_generator.generate(dependency_program,
                                    "dependency_globals");
  std::string dependency_ir;
  llvm::raw_string_ostream dependency_output{dependency_ir};
  dependency_module->print(dependency_output, nullptr);
  dependency_output.flush();
  const std::size_t initialize_base = dependency_ir.find(
      "store i32 %compute.result, ptr @__janus_global_entry__base");
  const std::size_t initialize_derived = dependency_ir.find(
      "store i32 %add, ptr @__janus_global_entry__derived");
  expect(initialize_base != std::string::npos &&
             initialize_derived != std::string::npos &&
             initialize_base < initialize_derived,
         "dynamic global dependencies are initialized topologically");
  expect(dependency_ir.find(
             "@__janus_global_entry__derived = global i32 0") !=
             std::string::npos,
         "a constant-shaped expression depending on a dynamic global is "
         "classified as dynamic");
  expect_compile_error(
      "val first : int = identity(second)\n"
      "val second : int = identity(first)\n"
      "def identity(value : int) : int { return value }\n"
      "def main() : int { return 0 }",
      "cyclic dynamic global dependency");

  janus::frontend::Parser aggregate_parser{R"(
enum Direction { North, East }
struct Point(val x : int, val y : int) {}
enum MaybePoint { Some(Point), None }
val direction : Direction = Direction.East
val origin : Point = new Point(3, 4)
val copied : Point = origin
val wrapped : MaybePoint = MaybePoint.Some(new Point(5, 6))
def main() : int {
    if direction == Direction.East { return origin.x + origin.y }
    return 0
}
)"};
  const janus::ast::Program aggregate_program =
      aggregate_parser.parse_program();
  static_cast<void>(analyzer.analyze(aggregate_program));
  llvm::LLVMContext aggregate_context;
  janus::backend::llvm::IrGenerator aggregate_generator{aggregate_context};
  const std::unique_ptr<llvm::Module> aggregate_module =
      aggregate_generator.generate(aggregate_program, "aggregate_globals");
  std::string aggregate_ir;
  llvm::raw_string_ostream aggregate_output{aggregate_ir};
  aggregate_module->print(aggregate_output, nullptr);
  aggregate_output.flush();
  expect(aggregate_ir.find(
             "@__janus_global_entry__direction = constant %enum.Direction") !=
             std::string::npos,
         "constant enum globals use native static storage");
  expect(aggregate_ir.find(
             "@__janus_global_entry__origin = constant %struct.Point") !=
             std::string::npos,
         "constant struct globals use inline static storage");
  expect(aggregate_ir.find("define internal void @__janus_init_globals") ==
             std::string::npos,
         "constant aggregate globals require no runtime initializer");
  expect(aggregate_ir.find(
             "@__janus_global_entry__wrapped = constant %enum.MaybePoint") !=
             std::string::npos,
         "constant enum payloads may contain constant structs");
  janus::frontend::Parser owning_aggregate_parser{R"(
class Item(val value : int) { destructor {} }
struct Box(val item : Item) {}
enum Holder { Some(Box), None }
val boxed : Box = new Box(new Item(1))
val held : Holder = Holder.Some(new Box(new Item(2)))
def main() : int { return boxed.item.value }
)"};
  const janus::ast::Program owning_aggregate_program =
      owning_aggregate_parser.parse_program();
  static_cast<void>(analyzer.analyze(owning_aggregate_program));
  llvm::LLVMContext owning_aggregate_context;
  janus::backend::llvm::IrGenerator owning_aggregate_generator{
      owning_aggregate_context};
  const std::unique_ptr<llvm::Module> owning_aggregate_module =
      owning_aggregate_generator.generate(owning_aggregate_program,
                                          "owning_aggregate_globals");
  std::string owning_aggregate_ir;
  llvm::raw_string_ostream owning_aggregate_output{owning_aggregate_ir};
  owning_aggregate_module->print(owning_aggregate_output, nullptr);
  owning_aggregate_output.flush();
  expect(owning_aggregate_ir.find("aggregate.struct.field") !=
             std::string::npos &&
             owning_aggregate_ir.find("aggregate.enum.payload") !=
                 std::string::npos,
         "owning struct and enum globals are finalized recursively");
  expect_compile_error(
      "class Item() {}\n"
      "struct Box(val item : Item) {}\n"
      "var boxed : Box = new Box(new Item())\n"
      "def main() : int { return 0 }",
      "owning global value 'boxed' must be declared with 'val'");

  janus::frontend::Parser owned_parser{R"(
class Resource(val value : int) {
    destructor {
    }
}
val resource : Resource = new Resource(42)
val callback : () => int = () => resource.value
val memory : Ptr[int] = alloc[int](usize(1))
def main() : int { return callback() }
)"};
  const janus::ast::Program owned_program = owned_parser.parse_program();
  static_cast<void>(analyzer.analyze(owned_program));
  llvm::LLVMContext owned_context;
  janus::backend::llvm::IrGenerator owned_generator{owned_context};
  const std::unique_ptr<llvm::Module> owned_module =
      owned_generator.generate(owned_program, "owned_globals");
  std::string owned_ir;
  llvm::raw_string_ostream owned_output{owned_ir};
  owned_module->print(owned_output, nullptr);
  owned_output.flush();
  const std::size_t finalizer =
      owned_ir.find("define internal void @__janus_fini_globals()");
  const std::size_t free_memory =
      owned_ir.find("call void @janus_free", finalizer);
  const std::size_t free_callback =
      owned_ir.find("call void @janus_free", free_memory + 1);
  const std::size_t destroy_resource =
      owned_ir.find("call void @Resource__destructor", free_callback);
  expect(finalizer != std::string::npos &&
             free_memory != std::string::npos &&
             free_callback != std::string::npos &&
             destroy_resource != std::string::npos &&
             free_memory < free_callback &&
             free_callback < destroy_resource,
         "owning globals are finalized in reverse declaration order");
  expect(owned_ir.find("call void @__janus_fini_globals()") !=
             std::string::npos,
         "entry point invokes global finalization");
  expect(owned_ir.find(
             "@__janus_globals_initialization_started = internal global i1 "
             "false") != std::string::npos &&
             owned_ir.find(
                 "@__janus_globals_finalization_finished = internal global "
                 "i1 false") != std::string::npos,
         "global initialization and finalization have idempotency guards");
  expect(owned_ir.find("@__janus_globals_initialized_count = internal global "
                       "i64 0") != std::string::npos &&
             owned_ir.find("icmp uge i64 %initialized.count") !=
                 std::string::npos,
         "the finalizer only releases globals initialized successfully");
  const std::size_t register_panic_cleanup =
      owned_ir.find("call void @janus_set_panic_cleanup");
  const std::size_t invoke_initializer =
      owned_ir.find("call void @__janus_init_globals()", register_panic_cleanup);
  expect(register_panic_cleanup != std::string::npos &&
             invoke_initializer != std::string::npos &&
             register_panic_cleanup < invoke_initializer,
         "panic cleanup is registered before dynamic global initialization");
  expect_compile_error(
      "class Resource() {}\n"
      "var resource : Resource = new Resource()\n"
      "def main() : int { return 0 }",
      "owning global value 'resource' must be declared with 'val'");
  expect_compile_error(
      "class Resource() {}\n"
      "val resource : Resource = new Resource()\n"
      "def take() : Resource { return move resource }\n"
      "def main() : int { return 0 }",
      "owning global value 'resource' cannot be moved");
  expect_compile_error(
      "class Resource() {}\n"
      "val resource : Resource = new Resource()\n"
      "def main() : int { delete resource return 0 }",
      "owning global value 'resource' is destroyed automatically");

  janus::frontend::ModuleLoader loader;
  const janus::ast::Program imported_program =
      loader.load(std::filesystem::path{JANUS_GLOBALS_ENTRY});
  const janus::semantic::AnalysisResult imported_analysis =
      analyzer.analyze(imported_program);
  expect(imported_analysis.globals.contains("global_config.secret") &&
             imported_analysis.globals.contains("other_config.secret"),
         "private globals use their qualified module identity");
  llvm::LLVMContext imported_context;
  janus::backend::llvm::IrGenerator imported_generator{imported_context};
  const std::unique_ptr<llvm::Module> imported_module =
      imported_generator.generate(imported_program, "imported_globals");
  std::string imported_ir;
  llvm::raw_string_ostream imported_output{imported_ir};
  imported_module->print(imported_output, nullptr);
  imported_output.flush();
  expect(imported_ir.find(
             "@__janus_global_global_config__secret = internal constant i32 7") !=
             std::string::npos,
         "imported private global is mangled with its module");
  expect(imported_ir.find(
             "@__janus_global_entry__localCount = constant i32 2") !=
             std::string::npos,
         "entry global uses the entry symbol namespace");
  expect(imported_ir.find(
             "store i32 3, ptr @__janus_global_global_config__importedCount") !=
             std::string::npos,
         "qualified assignment targets the requested module global");
  expect(imported_ir.find(
             "ptr @__janus_global_other_config__visibleCount") !=
             std::string::npos,
         "qualified read targets the requested module global");

  try {
    const janus::ast::Program private_access_program =
        loader.load(std::filesystem::path{JANUS_GLOBALS_PRIVATE_ACCESS});
    static_cast<void>(analyzer.analyze(private_access_program));
    expect(false, "an imported private global must not be visible");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "global value 'global_config.secret' is private") !=
               std::string_view::npos,
           "private global access is rejected outside its module");
  }

  try {
    const janus::ast::Program collision_program =
        loader.load(std::filesystem::path{JANUS_GLOBALS_PUBLIC_COLLISION});
    static_cast<void>(analyzer.analyze(collision_program));
    expect(false, "two public globals with the same name must conflict");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "public global value 'duplicated' is exported by both modules") !=
               std::string_view::npos,
           "public global collision identifies both exporting modules");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "global values are validated and resolved semantically\n";
  return 0;
}
