#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/backend/llvm/object_emitter.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/diagnostics/high_growth_loop_linter.hpp"
#include "janus/diagnostics/renderer.hpp"
#include "janus/driver/dependency.hpp"
#include "janus/driver/doctest.hpp"
#include "janus/driver/documentation.hpp"
#include "janus/driver/formatter.hpp"
#include "janus/driver/manifest.hpp"
#include "janus/driver/native_linker.hpp"
#include "janus/driver/project.hpp"
#include "janus/driver/registry.hpp"
#include "janus/driver/temporary_directory.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/semantic/analyzer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

struct Toolchain {
  std::filesystem::path stdlib;
  std::filesystem::path runtime;
  std::filesystem::path clang;
};

struct Options {
  std::string command;
  std::filesystem::path source;
  std::filesystem::path output;
  bool emit_llvm{};
  bool emit_object{};
  bool release{};
  bool locked{};
  bool offline{};
  bool format_check{};
  bool doc_open{};
  bool doc_stdlib{};
  bool doctests_only{};
  bool warn_high_growth_loops{};
  bool diagnostic_format_set{};
  bool panic_trace_set{};
  janus::backend::llvm::PanicTraceMode panic_trace{
      janus::backend::llvm::PanicTraceMode::Full};
  janus::diagnostics::DiagnosticFormat diagnostic_format{
      janus::diagnostics::DiagnosticFormat::Human};
  std::optional<janus::driver::Manifest> manifest;
  std::vector<std::filesystem::path> dependency_paths;
  std::vector<std::filesystem::path> documentation_paths;
  std::string test_filter;
};

class UsageError final : public std::runtime_error {
public:
  UsageError(std::string command, std::string message)
      : std::runtime_error{std::move(message)}, command_{std::move(command)} {}

  [[nodiscard]] const std::string &command() const noexcept { return command_; }

private:
  std::string command_;
};

std::filesystem::path executable_path(const char *argv0) {
#ifdef _WIN32
  std::wstring buffer(32768, L'\0');
  const DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                        static_cast<DWORD>(buffer.size()));
  if (size != 0 && size < buffer.size()) {
    buffer.resize(size);
    return std::filesystem::path{buffer};
  }
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) == 0)
    return std::filesystem::weakly_canonical(buffer.c_str());
#else
  std::error_code error;
  const std::filesystem::path path =
      std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error)
    return path;
#endif
  return std::filesystem::absolute(argv0);
}

Toolchain locate_toolchain(const char *argv0) {
  const std::filesystem::path root =
      executable_path(argv0).parent_path().parent_path();
  const std::filesystem::path installed_stdlib = root / "share/janus/stdlib";
#ifdef _WIN32
  const std::filesystem::path installed_runtime =
      root / "lib/janus_runtime.lib";
  const std::filesystem::path bundled_clang = root / "bin/clang.exe";
#else
  const std::filesystem::path installed_runtime =
      root / "lib/libjanus_runtime.a";
  const std::filesystem::path bundled_clang = root / "bin/clang";
#endif
  return {
      std::filesystem::exists(installed_stdlib)
          ? installed_stdlib
          : std::filesystem::path{JANUS_STDLIB_DIR},
      std::filesystem::exists(installed_runtime)
          ? installed_runtime
          : std::filesystem::path{JANUS_RUNTIME_LIBRARY},
      std::filesystem::exists(bundled_clang)
          ? bundled_clang
          : std::filesystem::path{JANUS_CLANG_PATH},
  };
}

void print_usage(std::ostream &output) {
  output << "usage:\n"
         << "  janus new <directory> [--name <name>]\n"
         << "  janus init [directory] [--name <name>]\n"
         << "  janus add <name>[@<version>] [--path <path> | "
            "--git <url> --rev <commit>]\n"
         << "  janus remove <name>\n"
         << "  janus publish\n"
         << "  janus check [source.janus] "
            "[--diagnostic-format human|json]\n"
         << "  janus build [source.janus] [-o output] [--release] "
            "[--emit llvm-ir|object] [--panic-trace full|short|off] "
            "[--diagnostic-format human|json]\n"
         << "  janus run [source.janus] [--release] "
            "[--panic-trace full|short|off]\n"
         << "  janus test [filter] [--doc] [--doc-path <path>] "
            "[--release] "
            "[--panic-trace full|short|off]\n"
         << "  janus fmt [source.janus] [--check]\n"
         << "  janus doc [--stdlib] [-o directory] [--open] [--offline]\n"
         << "  diagnostics: --warn-high-growth-loops for check, build, "
            "run\n"
         << "  dependency options: --locked --offline\n"
         << "  janus --help\n"
         << "  janus --version\n";
}

void print_command_usage(std::ostream &output, std::string_view command) {
  output << "usage: janus " << command;
  if (command == "check")
    output << " [source.janus] [--locked] [--offline] "
              "[--warn-high-growth-loops] "
              "[--diagnostic-format human|json]\n";
  else if (command == "build")
    output << " [source.janus] [-o output] [--release] "
              "[--emit llvm-ir|object] [--locked] [--offline] "
              "[--panic-trace full|short|off] "
              "[--warn-high-growth-loops] "
              "[--diagnostic-format human|json]\n";
  else if (command == "run")
    output << " [source.janus] [--release] [--locked] [--offline] "
              "[--panic-trace full|short|off] [--warn-high-growth-loops]\n";
  else if (command == "test")
    output << " [filter] [--doc] [--doc-path <path>] [--release] "
              "[--locked] [--offline] "
              "[--panic-trace full|short|off]\n";
  else if (command == "doc")
    output << " [--stdlib] [-o directory] [--open] [--offline]\n";
  else
    output << " [source.janus] [--check]\n";
}

bool is_execution_command(std::string_view command) {
  return command == "check" || command == "build" || command == "run" ||
         command == "test" || command == "doc";
}

void print_compile_error(const std::filesystem::path &path,
                         const janus::CompileError &error,
                         janus::diagnostics::DiagnosticFormat format =
                             janus::diagnostics::DiagnosticFormat::Human) {
  std::ifstream input{path, std::ios::binary};
  const std::string source{std::istreambuf_iterator<char>{input},
                           std::istreambuf_iterator<char>{}};
  std::size_t width = 80;
  if (const char *columns = std::getenv("COLUMNS")) {
    const unsigned long parsed = std::strtoul(columns, nullptr, 10);
    if (parsed >= 20)
      width = parsed;
  }
  std::cerr << janus::diagnostics::render_diagnostics(
      path, source, error.diagnostics(), format, {width});
}

int manage_package(int argc, char **argv) {
  const std::string command = argv[1];
  const std::filesystem::path manifest_path =
      janus::driver::find_manifest(std::filesystem::current_path());
  if (command == "publish") {
    if (argc != 2)
      throw std::runtime_error{"publish does not accept arguments"};
    const janus::driver::Manifest manifest =
        janus::driver::load_manifest(manifest_path);
    janus::driver::publish_package(manifest);
    std::cout << "published " << manifest.name << ' ' << manifest.version
              << " to " << janus::driver::registry_root().string() << '\n';
    return 0;
  }
  if (command == "remove") {
    if (argc != 3)
      throw std::runtime_error{"remove requires one dependency name"};
    janus::driver::remove_dependency(manifest_path, argv[2]);
    std::cout << "removed dependency '" << argv[2] << "'\n";
    return 0;
  }
  if (argc < 3)
    throw std::runtime_error{"add requires a dependency name"};
  janus::driver::Dependency dependency;
  dependency.name = argv[2];
  if (const std::size_t at = dependency.name.find('@');
      at != std::string::npos) {
    dependency.version_requirement = dependency.name.substr(at + 1);
    dependency.name.erase(at);
  }
  for (int index = 3; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--path") {
      if (++index == argc)
        throw std::runtime_error{"--path requires a directory"};
      dependency.path = argv[index];
    } else if (argument == "--git") {
      if (++index == argc)
        throw std::runtime_error{"--git requires a repository URL"};
      dependency.git = argv[index];
    } else if (argument == "--rev") {
      if (++index == argc)
        throw std::runtime_error{"--rev requires a commit hash"};
      dependency.revision = argv[index];
    } else if (argument == "--version") {
      if (++index == argc)
        throw std::runtime_error{"--version requires a requirement"};
      dependency.version_requirement = argv[index];
    } else {
      throw std::runtime_error{"unknown add option '" + std::string{argument} +
                               "'"};
    }
  }
  if (dependency.is_registry() && dependency.version_requirement.empty())
    dependency.version_requirement = "*";
  janus::driver::add_dependency(manifest_path, dependency);
  std::cout << "added dependency '" << dependency.name << "'\n";
  return 0;
}

int create_or_initialize(int argc, char **argv) {
  const std::string_view command = argv[1];
  int index = 2;
  std::filesystem::path directory;
  if (command == "new") {
    if (index == argc)
      throw std::runtime_error{"new requires a destination directory"};
    directory = argv[index++];
  } else if (index < argc && std::string_view{argv[index]} != "--name") {
    directory = argv[index++];
  } else {
    directory = std::filesystem::current_path();
  }
  std::string name;
  if (index < argc && std::string_view{argv[index]} == "--name") {
    if (++index == argc)
      throw std::runtime_error{"--name requires a project name"};
    name = argv[index++];
  }
  if (index != argc)
    throw std::runtime_error{"unexpected project creation argument"};
  if (command == "new")
    janus::driver::create_project(directory, name);
  else
    janus::driver::initialize_project(directory, name);
  std::cout << (command == "new" ? "created" : "initialized")
            << " Janus project in "
            << std::filesystem::absolute(directory).lexically_normal().string()
            << '\n';
  return 0;
}

Options parse_options(int argc, char **argv) {
  if (argc < 2)
    throw UsageError{"", "missing command"};
  Options options;
  options.command = argv[1];
  if (options.command != "check" && options.command != "build" &&
      options.command != "run" && options.command != "test" &&
      options.command != "fmt" && options.command != "doc")
    throw UsageError{"", "unknown command '" + options.command + "'"};
  for (int index = 2; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "-o") {
      if (++index == argc)
        throw UsageError{options.command, "-o requires an output path"};
      options.output = argv[index];
    } else if (argument == "--release") {
      options.release = true;
    } else if (argument == "--locked") {
      options.locked = true;
    } else if (argument == "--offline") {
      options.offline = true;
    } else if (argument == "--check" && options.command == "fmt") {
      options.format_check = true;
    } else if (argument == "--open" && options.command == "doc") {
      options.doc_open = true;
    } else if (argument == "--stdlib" && options.command == "doc") {
      options.doc_stdlib = true;
    } else if (argument == "--doc" && options.command == "test") {
      options.doctests_only = true;
    } else if (argument == "--doc-path" && options.command == "test") {
      if (++index == argc)
        throw UsageError{options.command,
                         "--doc-path requires a relative path"};
      options.documentation_paths.emplace_back(argv[index]);
    } else if (argument == "--warn-high-growth-loops") {
      options.warn_high_growth_loops = true;
    } else if (argument == "--panic-trace") {
      if (options.panic_trace_set)
        throw UsageError{options.command,
                         "--panic-trace may be specified only once"};
      if (++index == argc)
        throw UsageError{options.command,
                         "--panic-trace requires 'full', 'short', or 'off'"};
      const std::string_view mode = argv[index];
      if (mode == "full")
        options.panic_trace = janus::backend::llvm::PanicTraceMode::Full;
      else if (mode == "short")
        options.panic_trace = janus::backend::llvm::PanicTraceMode::Short;
      else if (mode == "off")
        options.panic_trace = janus::backend::llvm::PanicTraceMode::Off;
      else
        throw UsageError{options.command,
                         "--panic-trace accepts 'full', 'short', or 'off'"};
      options.panic_trace_set = true;
    } else if (argument == "--diagnostic-format") {
      if (options.diagnostic_format_set)
        throw UsageError{options.command,
                         "--diagnostic-format may be specified only once"};
      if (++index == argc)
        throw UsageError{options.command,
                         "--diagnostic-format requires 'human' or 'json'"};
      const std::string_view format = argv[index];
      if (format == "human")
        options.diagnostic_format = janus::diagnostics::DiagnosticFormat::Human;
      else if (format == "json")
        options.diagnostic_format = janus::diagnostics::DiagnosticFormat::Json;
      else
        throw UsageError{options.command,
                         "--diagnostic-format accepts 'human' or 'json'"};
      options.diagnostic_format_set = true;
    } else if (argument == "--emit") {
      if (options.emit_llvm || options.emit_object)
        throw UsageError{options.command, "--emit may be specified only once"};
      if (++index == argc)
        throw UsageError{options.command,
                         "--emit requires 'llvm-ir' or 'object'"};
      const std::string_view kind = argv[index];
      if (kind == "llvm-ir")
        options.emit_llvm = true;
      else if (kind == "object")
        options.emit_object = true;
      else
        throw UsageError{options.command,
                         "--emit accepts 'llvm-ir' or 'object'"};
    } else if (!argument.starts_with('-')) {
      if (options.command == "test") {
        if (!options.test_filter.empty())
          throw UsageError{options.command, "test accepts at most one filter"};
        options.test_filter = argv[index];
      } else if (options.command == "doc") {
        throw UsageError{options.command, "doc does not accept a source path"};
      } else {
        if (!options.source.empty())
          throw UsageError{options.command, "multiple source paths provided"};
        options.source = argv[index];
      }
    } else {
      throw UsageError{options.command,
                       "unknown option '" + std::string{argument} + "'"};
    }
  }
  if (options.command == "check" &&
      (!options.output.empty() || options.emit_llvm || options.emit_object ||
       options.release || options.panic_trace_set))
    throw UsageError{options.command, "check does not accept build options"};
  if (options.command == "run" &&
      (!options.output.empty() || options.emit_llvm || options.emit_object))
    throw UsageError{options.command, "run does not accept -o or --emit"};
  if (options.command == "test" &&
      (!options.output.empty() || options.emit_llvm || options.emit_object))
    throw UsageError{options.command, "test does not accept -o or --emit"};
  if (options.command == "fmt" &&
      (!options.output.empty() || options.emit_llvm || options.emit_object ||
       options.release || options.locked || options.offline ||
       options.warn_high_growth_loops || options.panic_trace_set))
    throw UsageError{options.command,
                     "fmt only accepts a source path and --check"};
  if (options.command == "doc" &&
      (options.release || options.locked || options.format_check ||
       options.emit_llvm || options.emit_object ||
       options.warn_high_growth_loops || options.panic_trace_set ||
       options.diagnostic_format_set))
    throw UsageError{options.command,
                     "doc only accepts --stdlib, -o, --open, and --offline"};
  if ((options.command == "test") && options.warn_high_growth_loops)
    throw UsageError{options.command,
                     "test does not accept --warn-high-growth-loops"};
  if (options.diagnostic_format_set && options.command != "check" &&
      options.command != "build")
    throw UsageError{
        options.command,
        "--diagnostic-format is only available for check and build"};
  if (options.release && !options.panic_trace_set)
    options.panic_trace = janus::backend::llvm::PanicTraceMode::Short;
  if (options.source.empty() && !options.doc_stdlib) {
    options.manifest = janus::driver::load_manifest(
        janus::driver::find_manifest(std::filesystem::current_path()));
    options.source = options.manifest->entry_path();
  }
  return options;
}

std::string shell_quote(const std::filesystem::path &path) {
  const std::string value = path.string();
#ifdef _WIN32
  std::string quoted{"\""};
  for (const char character : value) {
    if (character == '"')
      quoted += '\\';
    quoted += character;
  }
  return quoted + '"';
#else
  std::string quoted{"'"};
  for (const char character : value) {
    if (character == '\'')
      quoted += "'\\''";
    else
      quoted += character;
  }
  return quoted + '\'';
#endif
}

int command_status(int status) {
  if (status == -1)
    return 1;
#ifdef _WIN32
  return status;
#else
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return 1;
#endif
}

std::unique_ptr<llvm::Module>
compile(const std::filesystem::path &source, llvm::LLVMContext &context,
        const Toolchain &toolchain,
        const std::vector<std::filesystem::path> &dependency_paths,
        bool warn_high_growth_loops,
        janus::backend::llvm::PanicTraceMode panic_trace) {
  std::vector<std::filesystem::path> search_paths{toolchain.stdlib};
  search_paths.insert(search_paths.end(), dependency_paths.begin(),
                      dependency_paths.end());
  janus::frontend::ModuleLoader loader{std::move(search_paths)};
  const janus::ast::Program program = loader.load(source);
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));
  if (warn_high_growth_loops) {
    for (const janus::diagnostics::HighGrowthLoopWarning &warning :
         janus::diagnostics::find_high_growth_loop_warnings(program)) {
      std::cerr << source.string() << ':' << warning.location.line << ':'
                << warning.location.column
                << ": warning: high-growth loop pattern may cause integer "
                   "overflow or excessive running time; add an explicit "
                   "bound, use a safe numeric type, or enforce a time budget\n";
    }
  }
  janus::backend::llvm::IrGenerator generator{context};
  std::unique_ptr<llvm::Module> module =
      generator.generate(program, source.string(), panic_trace);
  if (llvm::verifyModule(*module, &llvm::errs()))
    throw std::runtime_error{"generated invalid LLVM IR"};
  return module;
}

void write_ir(const llvm::Module &module, const std::filesystem::path &path) {
  std::error_code error;
  llvm::raw_fd_ostream output{path.string(), error, llvm::sys::fs::OF_None};
  if (error)
    throw std::runtime_error{"cannot create '" + path.string() +
                             "': " + error.message()};
  module.print(output, nullptr);
}

std::filesystem::path default_output(const Options &options) {
  if (!options.output.empty())
    return options.output;
  if (options.manifest.has_value()) {
    std::filesystem::path output = options.manifest->root() / "target" /
                                   (options.release ? "release" : "debug") /
                                   options.manifest->name;
    if (options.emit_llvm)
      output += ".ll";
    else if (options.emit_object)
      output += ".o";
#ifdef _WIN32
    else
      output += ".exe";
#endif
    return output;
  }
  if (options.emit_llvm) {
    std::filesystem::path output = options.source.filename();
    output.replace_extension(".ll");
    return output;
  }
  if (options.emit_object) {
    std::filesystem::path output = options.source.filename();
    output.replace_extension(".o");
    return output;
  }
  std::filesystem::path output = options.source.filename();
  output.replace_extension();
#ifdef _WIN32
  output += ".exe";
#endif
  return output;
}

int build(const Options &options, const std::filesystem::path &output,
          const Toolchain &toolchain) {
  if (!output.parent_path().empty())
    std::filesystem::create_directories(output.parent_path());
  llvm::LLVMContext context;
  std::unique_ptr<llvm::Module> module =
      compile(options.source, context, toolchain, options.dependency_paths,
              options.warn_high_growth_loops, options.panic_trace);
  if (options.emit_llvm) {
    write_ir(*module, output);
    return 0;
  }

  std::optional<janus::driver::TemporaryDirectory> temporary_directory;
  std::filesystem::path object = output;
  if (!options.emit_object) {
    temporary_directory.emplace(
        janus::driver::TemporaryDirectory::create("janus-build"));
    object = temporary_directory->path() / "module.o";
  }
  janus::backend::llvm::emit_object(*module, object, options.release);
  if (options.emit_object)
    return 0;
  janus::driver::link_executable({object}, output,
                                 janus::driver::LinkOptions{!options.release,
                                                            {toolchain.runtime},
                                                            toolchain.clang});
  return 0;
}

int run_tests(const Options &options, const Toolchain &toolchain) {
  const std::filesystem::path tests_root = options.manifest->root() / "tests";
  std::vector<std::filesystem::path> tests;
  if (!options.doctests_only && std::filesystem::is_directory(tests_root)) {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(tests_root)) {
      if (entry.is_regular_file() && entry.path().extension() == ".janus" &&
          (options.test_filter.empty() ||
           entry.path().generic_string().find(options.test_filter) !=
               std::string::npos))
        tests.push_back(entry.path());
    }
  }
  std::sort(tests.begin(), tests.end());

  std::vector<std::filesystem::path> documentation_paths =
      options.documentation_paths;
  if (documentation_paths.empty())
    documentation_paths = {"README.md", "docs"};
  std::vector<janus::driver::Doctest> doctests =
      janus::driver::discover_doctests(options.manifest->root(),
                                       documentation_paths);
  std::erase_if(doctests, [&options](const janus::driver::Doctest &test) {
    return !janus::driver::matches_doctest_filter(test, options.test_filter);
  });

  std::size_t passed = 0;
  for (const std::filesystem::path &test : tests) {
    std::filesystem::path relative =
        std::filesystem::relative(test, tests_root);
    relative.replace_extension();
    std::filesystem::path executable = options.manifest->root() / "target" /
                                       (options.release ? "release" : "debug") /
                                       "tests" / relative;
#ifdef _WIN32
    executable += ".exe";
#endif
    std::cout << "test " << relative.generic_string() << " ... " << std::flush;
    try {
      Options test_options = options;
      test_options.source = test;
      test_options.warn_high_growth_loops = false;
      build(test_options, executable, toolchain);
      const int status =
          command_status(std::system(shell_quote(executable).c_str()));
      if (status == 0) {
        ++passed;
        std::cout << "ok\n";
      } else {
        std::cout << "FAILED (exit " << status << ")\n";
      }
    } catch (const janus::CompileError &error) {
      std::cout << "FAILED\n";
      print_compile_error(test, error);
    } catch (const std::exception &error) {
      std::cout << "FAILED\n";
      std::cerr << test.string() << ": " << error.what() << '\n';
    }
  }

  janus::driver::TemporaryDirectory doctest_directory =
      janus::driver::TemporaryDirectory::create("janus-doctest");
  for (std::size_t index = 0; index < doctests.size(); ++index) {
    const janus::driver::Doctest &test = doctests[index];
    const std::filesystem::path source =
        doctest_directory.path() /
        ("doctest-" + std::to_string(index) + ".janus");
    {
      std::ofstream output{source, std::ios::binary};
      if (!output)
        throw std::runtime_error{"cannot create doctest source"};
      output << test.source;
    }
    std::cout << "doctest " << test.display_name() << " ... " << std::flush;
    try {
      llvm::LLVMContext context;
      static_cast<void>(compile(source, context, toolchain,
                                options.dependency_paths, false,
                                options.panic_trace));
      if (test.expectation == janus::driver::DoctestExpectation::CompilePass) {
        ++passed;
        std::cout << "ok\n";
      } else {
        std::cout << "FAILED (expected diagnostic " << test.expected_diagnostic
                  << ")\n";
        std::cerr << test.document.generic_string() << ':' << test.line
                  << ": error: doctest compiled successfully, expected "
                  << test.expected_diagnostic << '\n';
      }
    } catch (const janus::CompileError &error) {
      const auto expected = [&test, &error] {
        if (test.expectation != janus::driver::DoctestExpectation::CompileFail)
          return false;
        return std::any_of(
            error.diagnostics().begin(), error.diagnostics().end(),
            [&test](const janus::Diagnostic &diagnostic) {
              return janus::diagnostic_code_name(diagnostic.code) ==
                     test.expected_diagnostic;
            });
      }();
      if (expected) {
        ++passed;
        std::cout << "ok\n";
      } else {
        std::cout << "FAILED\n";
        std::cerr << test.document.generic_string() << ':' << test.line
                  << ": error: ";
        if (test.expectation == janus::driver::DoctestExpectation::CompileFail)
          std::cerr << "expected diagnostic " << test.expected_diagnostic
                    << ", got "
                    << janus::diagnostic_code_name(error.diagnostic().code);
        else
          std::cerr << "doctest compilation failed with "
                    << janus::diagnostic_code_name(error.diagnostic().code)
                    << ": " << error.what();
        std::cerr << '\n';
      }
    } catch (const std::exception &error) {
      std::cout << "FAILED\n";
      std::cerr << test.document.generic_string() << ':' << test.line
                << ": error: " << error.what() << '\n';
    }
  }
  const std::size_t total = tests.size() + doctests.size();
  std::cout << "\ntest result: " << (passed == total ? "ok" : "FAILED") << ". "
            << passed << " passed; " << total - passed << " failed\n";
  return passed == total ? 0 : 1;
}

int format_sources(const Options &options) {
  std::vector<std::filesystem::path> sources;
  if (options.manifest.has_value()) {
    for (const char *directory : {"src", "tests"}) {
      const std::filesystem::path root = options.manifest->root() / directory;
      if (!std::filesystem::is_directory(root))
        continue;
      for (const auto &entry :
           std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".janus")
          sources.push_back(entry.path());
      }
    }
  } else {
    sources.push_back(options.source);
  }
  std::sort(sources.begin(), sources.end());
  const std::filesystem::path configuration =
      options.manifest.has_value() ? options.manifest->root() / ".janusfmt"
                                   : options.source.parent_path() / ".janusfmt";
  const janus::driver::FormatOptions format_options =
      janus::driver::load_format_options(configuration);
  bool changed = false;
  for (const std::filesystem::path &source : sources) {
    std::ifstream input{source};
    if (!input)
      throw std::runtime_error{"cannot read '" + source.string() + "'"};
    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    const std::string formatted =
        janus::driver::format_source(contents, format_options);
    if (formatted == contents)
      continue;
    changed = true;
    if (options.format_check) {
      std::cout << "would format " << source.string() << '\n';
      continue;
    }
    std::ofstream output{source, std::ios::trunc};
    if (!output)
      throw std::runtime_error{"cannot write '" + source.string() + "'"};
    output << formatted;
    std::cout << "formatted " << source.string() << '\n';
  }
  return options.format_check && changed ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--help") {
    print_usage(std::cout);
    return 0;
  }
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::cout << "janus " << JANUS_VERSION << '\n';
    return 0;
  }
  if (argc == 3 && is_execution_command(argv[1]) &&
      std::string_view{argv[2]} == "--help") {
    print_command_usage(std::cout, argv[1]);
    return 0;
  }

  std::filesystem::path diagnostic_path;
  janus::diagnostics::DiagnosticFormat diagnostic_format =
      janus::diagnostics::DiagnosticFormat::Human;
  try {
    if (argc >= 2 && (std::string_view{argv[1]} == "new" ||
                      std::string_view{argv[1]} == "init"))
      return create_or_initialize(argc, argv);
    if (argc >= 2 && (std::string_view{argv[1]} == "add" ||
                      std::string_view{argv[1]} == "remove" ||
                      std::string_view{argv[1]} == "publish"))
      return manage_package(argc, argv);
    Options options = parse_options(argc, argv);
    diagnostic_format = options.diagnostic_format;
    if (options.command == "fmt")
      return format_sources(options);
    if (options.command == "doc") {
      const std::filesystem::path output =
          options.output.empty()
              ? options.doc_stdlib
                    ? std::filesystem::current_path() / "target" / "doc" /
                          "stdlib"
                    : options.manifest->root() / "target" / "doc"
              : std::filesystem::absolute(options.output).lexically_normal();
      const janus::driver::DocumentationReport report = options.doc_stdlib
          ? janus::driver::generate_stdlib_documentation(
                locate_toolchain(argv[0]).stdlib, output, JANUS_VERSION)
          : janus::driver::generate_package_documentation(*options.manifest,
                                                          output);
      for (const janus::driver::UnresolvedDocumentationLink &link :
           report.unresolved_links)
        std::cerr << "warning: unresolved documentation link '[[" << link.symbol
                  << "]]' in " << link.context << '\n';
      if (options.doc_stdlib) {
        for (const std::string &module : report.undocumented_modules)
          std::cerr << "error: undocumented standard-library module "
                    << module << '\n';
        for (const std::string &symbol : report.undocumented_symbols)
          std::cerr << "error: undocumented standard-library symbol "
                    << symbol << '\n';
        if (!report.unresolved_links.empty() ||
            !report.undocumented_modules.empty() ||
            !report.undocumented_symbols.empty())
          return 1;
      }
      std::cout << "generated " << report.symbol_count << " public symbols in "
                << report.index_path.string() << '\n';
      if (options.doc_open)
        janus::driver::open_documentation(report.index_path);
      return 0;
    }
    const Toolchain toolchain = locate_toolchain(argv[0]);
    if (options.manifest.has_value())
      options.dependency_paths = janus::driver::resolve_dependencies(
          *options.manifest, {options.locked, options.offline});
    if (options.command == "test") {
      options.dependency_paths.push_back(options.manifest->root() / "src");
      return run_tests(options, toolchain);
    }
    diagnostic_path = options.source;
    if (options.command == "check") {
      llvm::LLVMContext context;
      static_cast<void>(
          compile(options.source, context, toolchain, options.dependency_paths,
                  options.warn_high_growth_loops, options.panic_trace));
      std::cout << "checked " << options.source.string() << '\n';
      return 0;
    }
    if (options.command == "build")
      return build(options, default_output(options), toolchain);

    const bool temporary = !options.manifest.has_value();
    std::optional<janus::driver::TemporaryDirectory> run_directory;
    std::filesystem::path executable;
    if (temporary) {
      run_directory.emplace(
          janus::driver::TemporaryDirectory::create("janus-run"));
      executable = run_directory->path() / "program";
#ifdef _WIN32
      executable += ".exe";
#endif
    } else {
      executable = default_output(options);
    }
    const int build_status = build(options, executable, toolchain);
    if (build_status != 0)
      return build_status;
    const int run_status =
        command_status(std::system(shell_quote(executable).c_str()));
    return run_status;
  } catch (const UsageError &error) {
    std::cerr << "janus";
    if (!error.command().empty())
      std::cerr << ' ' << error.command();
    std::cerr << ": error: " << error.what() << '\n';
    if (error.command().empty())
      print_usage(std::cerr);
    else
      print_command_usage(std::cerr, error.command());
    return 2;
  } catch (const janus::CompileError &error) {
    print_compile_error(diagnostic_path, error, diagnostic_format);
  } catch (const std::exception &error) {
    std::cerr << "janus: error: " << error.what() << '\n';
  }
  return 1;
}
