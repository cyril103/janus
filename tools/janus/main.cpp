#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/backend/llvm/object_emitter.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/driver/dependency.hpp"
#include "janus/driver/formatter.hpp"
#include "janus/driver/manifest.hpp"
#include "janus/driver/native_linker.hpp"
#include "janus/driver/project.hpp"
#include "janus/driver/registry.hpp"
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
  std::optional<janus::driver::Manifest> manifest;
  std::vector<std::filesystem::path> dependency_paths;
  std::string test_filter;
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

void print_usage() {
  std::cerr << "usage:\n"
            << "  janus new <directory> [--name <name>]\n"
            << "  janus init [directory] [--name <name>]\n"
            << "  janus add <name>[@<version>] [--path <path> | "
               "--git <url> --rev <commit>]\n"
            << "  janus remove <name>\n"
            << "  janus publish\n"
            << "  janus check [source.janus]\n"
            << "  janus build [source.janus] [-o output] [--release] "
               "[--emit llvm-ir|object]\n"
            << "  janus run [source.janus] [--release]\n"
            << "  janus test [filter] [--release]\n"
            << "  janus fmt [source.janus] [--check]\n"
            << "  dependency options: --locked --offline\n"
            << "  janus --version\n";
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
    throw std::runtime_error{"missing command"};
  Options options;
  options.command = argv[1];
  if (options.command != "check" && options.command != "build" &&
      options.command != "run" && options.command != "test" &&
      options.command != "fmt")
    throw std::runtime_error{"unknown command '" + options.command + "'"};
  int first_option = 2;
  if (first_option < argc &&
      !std::string_view{argv[first_option]}.starts_with('-')) {
    if (options.command == "test")
      options.test_filter = argv[first_option++];
    else
      options.source = argv[first_option++];
  }
  for (int index = first_option; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "-o") {
      if (++index == argc)
        throw std::runtime_error{"-o requires an output path"};
      options.output = argv[index];
    } else if (argument == "--release") {
      options.release = true;
    } else if (argument == "--locked") {
      options.locked = true;
    } else if (argument == "--offline") {
      options.offline = true;
    } else if (argument == "--check" && options.command == "fmt") {
      options.format_check = true;
    } else if (argument == "--emit") {
      if (++index == argc)
        throw std::runtime_error{"--emit requires 'llvm-ir' or 'object'"};
      const std::string_view kind = argv[index];
      if (kind == "llvm-ir")
        options.emit_llvm = true;
      else if (kind == "object")
        options.emit_object = true;
      else
        throw std::runtime_error{"--emit accepts 'llvm-ir' or 'object'"};
    } else {
      throw std::runtime_error{"unknown option '" + std::string{argument} +
                               "'"};
    }
  }
  if (options.command == "check" &&
      (!options.output.empty() || options.emit_llvm || options.emit_object ||
       options.release))
    throw std::runtime_error{"check does not accept build options"};
  if (options.command == "run" &&
      (!options.output.empty() || options.emit_llvm || options.emit_object))
    throw std::runtime_error{"run does not accept -o or --emit"};
  if (options.command == "test" &&
      (!options.output.empty() || options.emit_llvm || options.emit_object))
    throw std::runtime_error{"test does not accept -o or --emit"};
  if (options.command == "fmt" &&
      (!options.output.empty() || options.emit_llvm || options.emit_object ||
       options.release || options.locked || options.offline))
    throw std::runtime_error{"fmt only accepts a source path and --check"};
  if (options.source.empty()) {
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
        const std::vector<std::filesystem::path> &dependency_paths) {
  std::vector<std::filesystem::path> search_paths{toolchain.stdlib};
  search_paths.insert(search_paths.end(), dependency_paths.begin(),
                      dependency_paths.end());
  janus::frontend::ModuleLoader loader{std::move(search_paths)};
  const janus::ast::Program program = loader.load(source);
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));
  janus::backend::llvm::IrGenerator generator{context};
  std::unique_ptr<llvm::Module> module =
      generator.generate(program, source.string());
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
      compile(options.source, context, toolchain, options.dependency_paths);
  if (options.emit_llvm) {
    write_ir(*module, output);
    return 0;
  }

  const std::filesystem::path object =
      options.emit_object ? output
                          : std::filesystem::temp_directory_path() /
                                ("janus-" + std::to_string(std::rand()) + ".o");
  janus::backend::llvm::emit_object(*module, object, options.release);
  if (options.emit_object)
    return 0;
  janus::driver::link_executable({object}, output,
                                 janus::driver::LinkOptions{!options.release,
                                                            {toolchain.runtime},
                                                            toolchain.clang});
  std::error_code ignored;
  std::filesystem::remove(object, ignored);
  return 0;
}

int run_tests(const Options &options, const Toolchain &toolchain) {
  const std::filesystem::path tests_root = options.manifest->root() / "tests";
  std::vector<std::filesystem::path> tests;
  if (std::filesystem::is_directory(tests_root)) {
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
      build(test_options, executable, toolchain);
      const int status =
          command_status(std::system(shell_quote(executable).c_str()));
      if (status == 0) {
        ++passed;
        std::cout << "ok\n";
      } else {
        std::cout << "FAILED (exit " << status << ")\n";
      }
    } catch (const std::exception &error) {
      std::cout << "FAILED\n";
      std::cerr << test.string() << ": " << error.what() << '\n';
    }
  }
  std::cout << "\ntest result: " << (passed == tests.size() ? "ok" : "FAILED")
            << ". " << passed << " passed; " << tests.size() - passed
            << " failed\n";
  return passed == tests.size() ? 0 : 1;
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
      options.manifest.has_value()
          ? options.manifest->root() / ".janusfmt"
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
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::cout << "janus " << JANUS_VERSION << '\n';
    return 0;
  }

  std::filesystem::path diagnostic_path;
  try {
    if (argc >= 2 && (std::string_view{argv[1]} == "new" ||
                      std::string_view{argv[1]} == "init"))
      return create_or_initialize(argc, argv);
    if (argc >= 2 && (std::string_view{argv[1]} == "add" ||
                      std::string_view{argv[1]} == "remove" ||
                      std::string_view{argv[1]} == "publish"))
      return manage_package(argc, argv);
    const Toolchain toolchain = locate_toolchain(argv[0]);
    Options options = parse_options(argc, argv);
    if (options.command == "fmt")
      return format_sources(options);
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
      static_cast<void>(compile(options.source, context, toolchain,
                                options.dependency_paths));
      std::cout << "checked " << options.source.string() << '\n';
      return 0;
    }
    if (options.command == "build")
      return build(options, default_output(options), toolchain);

    const bool temporary = !options.manifest.has_value();
    const std::filesystem::path executable =
        temporary ? std::filesystem::temp_directory_path() /
                        ("janus-run-" + std::to_string(std::rand())
#ifdef _WIN32
                         + ".exe"
#endif
                         )
                  : default_output(options);
    const int build_status = build(options, executable, toolchain);
    if (build_status != 0)
      return build_status;
    const int run_status =
        command_status(std::system(shell_quote(executable).c_str()));
    std::error_code ignored;
    if (temporary)
      std::filesystem::remove(executable, ignored);
    return run_status;
  } catch (const janus::CompileError &error) {
    const janus::SourceLocation location = error.location();
    std::cerr << diagnostic_path.string() << ':' << location.line << ':'
              << location.column << ": error: " << error.what() << '\n';
  } catch (const std::exception &error) {
    std::cerr << "janus: error: " << error.what() << '\n';
    print_usage();
  }
  return 1;
}
