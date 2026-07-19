#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/backend/llvm/object_emitter.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/driver/native_linker.hpp"
#include "janus/driver/project.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/semantic/analyzer.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
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
            << "  janus check <source.janus>\n"
            << "  janus build <source.janus> [-o output] [--release] "
               "[--emit llvm-ir|object]\n"
            << "  janus run <source.janus> [--release]\n"
            << "  janus --version\n";
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
  if (argc < 3)
    throw std::runtime_error{"missing command or source file"};
  Options options;
  options.command = argv[1];
  options.source = argv[2];
  if (options.command != "check" && options.command != "build" &&
      options.command != "run")
    throw std::runtime_error{"unknown command '" + options.command + "'"};
  for (int index = 3; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "-o") {
      if (++index == argc)
        throw std::runtime_error{"-o requires an output path"};
      options.output = argv[index];
    } else if (argument == "--release") {
      options.release = true;
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

std::unique_ptr<llvm::Module> compile(const std::filesystem::path &source,
                                      llvm::LLVMContext &context,
                                      const Toolchain &toolchain) {
  janus::frontend::ModuleLoader loader{{toolchain.stdlib}};
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
  llvm::LLVMContext context;
  std::unique_ptr<llvm::Module> module =
      compile(options.source, context, toolchain);
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
    const Toolchain toolchain = locate_toolchain(argv[0]);
    const Options options = parse_options(argc, argv);
    diagnostic_path = options.source;
    if (options.command == "check") {
      llvm::LLVMContext context;
      static_cast<void>(compile(options.source, context, toolchain));
      std::cout << "checked " << options.source.string() << '\n';
      return 0;
    }
    if (options.command == "build")
      return build(options, default_output(options), toolchain);

    const std::filesystem::path executable =
        std::filesystem::temp_directory_path() /
        ("janus-run-" + std::to_string(std::rand())
#ifdef _WIN32
         + ".exe"
#endif
        );
    const int build_status = build(options, executable, toolchain);
    if (build_status != 0)
      return build_status;
    const int run_status =
        command_status(std::system(shell_quote(executable).c_str()));
    std::error_code ignored;
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
