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
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "global diagnostic contains the expected explanation");
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
      "must use a statically initialized built-in value type");
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
