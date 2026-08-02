#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/backend/llvm/object_emitter.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/diagnostics/high_growth_loop_linter.hpp"
#include "janus/diagnostics/renderer.hpp"
#include "janus/driver/dependency.hpp"
#include "janus/driver/doctest.hpp"
#include "janus/driver/documentation.hpp"
#include "janus/driver/formatter.hpp"
#include "janus/driver/incremental_cache.hpp"
#include "janus/driver/manifest.hpp"
#include "janus/driver/native_linker.hpp"
#include "janus/driver/project.hpp"
#include "janus/driver/registry.hpp"
#include "janus/driver/temporary_directory.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/semantic/analyzer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/TargetParser/Host.h>
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
  bool no_cache{};
  bool format_check{};
  bool doc_open{};
  bool doc_stdlib{};
  bool doctests_only{};
  bool warn_high_growth_loops{};
  enum class TimingsFormat { None, Human, Json } timings_format{
      TimingsFormat::None};
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

struct CompilationTimings {
  using Clock = std::chrono::steady_clock;
  using Duration = std::chrono::nanoseconds;

  Duration loading{};
  Duration parsing{};
  Duration analysis{};
  Duration llvm_generation{};
  Duration optimization{};
  Duration link{};
  Duration overhead{};
  Duration total{};
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
            "--git <url> --rev <commit>] [--registry <url>]\n"
         << "  janus remove <name>\n"
         << "  janus search <query> [--registry <url>]\n"
         << "  janus publish [--registry <url>]\n"
         << "  janus clean\n"
         << "  janus check [source.janus] "
            "[--diagnostic-format human|json]\n"
         << "  janus build [source.janus] [-o output] [--release] "
            "[--emit llvm-ir|object] [--panic-trace full|short|off] "
            "[--diagnostic-format human|json] [--timings[=human|json]] "
            "[--no-cache]\n"
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
              "[--diagnostic-format human|json] "
              "[--timings[=human|json]] [--no-cache]\n";
  else if (command == "run")
    output << " [source.janus] [--release] [--locked] [--offline] "
              "[--panic-trace full|short|off] [--warn-high-growth-loops]\n";
  else if (command == "test")
    output << " [filter] [--doc] [--doc-path <path>] [--release] "
              "[--locked] [--offline] "
              "[--panic-trace full|short|off]\n";
  else if (command == "doc")
    output << " [--stdlib] [-o directory] [--open] [--offline]\n";
  else if (command == "clean")
    output << '\n';
  else
    output << " [source.janus] [--check]\n";
}

bool is_execution_command(std::string_view command) {
  return command == "check" || command == "build" || command == "run" ||
         command == "test" || command == "doc" || command == "clean";
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
  if (command == "search") {
    if (argc < 3)
      throw std::runtime_error{"search requires a query"};
    const std::string query = argv[2];
    std::string registry;
    for (int index = 3; index < argc; ++index) {
      if (std::string_view{argv[index]} != "--registry" || ++index == argc)
        throw std::runtime_error{
            "search only accepts a query and --registry <url>"};
      registry = argv[index];
    }
    const auto results = janus::driver::search_registry(query, registry);
    for (const janus::driver::RegistrySearchResult &result : results) {
      std::cout << result.package << ' ' << result.latest_version;
      if (!result.description.empty())
        std::cout << " - " << result.description;
      std::cout << '\n';
    }
    return 0;
  }
  const std::filesystem::path manifest_path =
      janus::driver::find_manifest(std::filesystem::current_path());
  if (command == "publish") {
    std::string registry;
    if (argc != 2) {
      if (argc != 4 || std::string_view{argv[2]} != "--registry")
        throw std::runtime_error{"publish only accepts --registry <url>"};
      registry = argv[3];
    }
    const janus::driver::Manifest manifest =
        janus::driver::load_manifest(manifest_path);
    janus::driver::publish_package(manifest, registry);
    const std::string location =
        registry.empty() ? janus::driver::registry_location() : registry;
    std::cout << "published " << manifest.name << ' ' << manifest.version
              << " to " << location << '\n';
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
    } else if (argument == "--registry") {
      if (++index == argc)
        throw std::runtime_error{"--registry requires a URL"};
      dependency.registry = argv[index];
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
      options.command != "fmt" && options.command != "doc" &&
      options.command != "clean")
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
    } else if (argument == "--no-cache") {
      options.no_cache = true;
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
    } else if (argument == "--timings" ||
               argument.starts_with("--timings=")) {
      if (options.timings_format != Options::TimingsFormat::None)
        throw UsageError{options.command,
                         "--timings may be specified only once"};
      const std::string_view format =
          argument == "--timings" ? std::string_view{"human"}
                                  : argument.substr(10);
      if (format == "human")
        options.timings_format = Options::TimingsFormat::Human;
      else if (format == "json")
        options.timings_format = Options::TimingsFormat::Json;
      else
        throw UsageError{options.command,
                         "--timings accepts 'human' or 'json'"};
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
       options.warn_high_growth_loops || options.panic_trace_set ||
       options.no_cache))
    throw UsageError{options.command,
                     "fmt only accepts a source path and --check"};
  if (options.command == "doc" &&
      (options.release || options.locked || options.format_check ||
       options.emit_llvm || options.emit_object ||
       options.warn_high_growth_loops || options.panic_trace_set ||
       options.diagnostic_format_set || options.no_cache))
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
  if (options.timings_format != Options::TimingsFormat::None &&
      options.command != "build")
    throw UsageError{options.command,
                     "--timings is only available for build"};
  if (options.no_cache && options.command != "build")
    throw UsageError{options.command,
                     "--no-cache is only available for build"};
  if (options.command == "clean" &&
      (!options.source.empty() || !options.output.empty() || options.release ||
       options.locked || options.offline || options.no_cache ||
       options.format_check || options.doc_open || options.doc_stdlib ||
       options.doctests_only || options.warn_high_growth_loops ||
       options.timings_format != Options::TimingsFormat::None ||
       options.diagnostic_format_set || options.panic_trace_set ||
       options.emit_llvm || options.emit_object))
    throw UsageError{options.command, "clean does not accept arguments"};
  if (options.release && !options.panic_trace_set)
    options.panic_trace = janus::backend::llvm::PanicTraceMode::Short;
  if (options.source.empty() && !options.doc_stdlib &&
      options.command != "clean") {
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
        janus::diagnostics::DiagnosticFormat diagnostic_format,
        janus::backend::llvm::PanicTraceMode panic_trace,
        CompilationTimings *timings = nullptr,
        bool dependencies_only = false,
        std::string_view source_name_override = {}) {
  std::vector<std::filesystem::path> search_paths{toolchain.stdlib};
  search_paths.insert(search_paths.end(), dependency_paths.begin(),
                      dependency_paths.end());
  janus::frontend::ModuleLoader loader{std::move(search_paths)};
  janus::frontend::ModuleLoadTimings load_timings;
  const janus::ast::Program program =
      loader.load(source, timings == nullptr ? nullptr : &load_timings);
  if (timings != nullptr) {
    timings->loading = load_timings.loading;
    timings->parsing = load_timings.parsing;
  }
  const auto analysis_start =
      timings == nullptr ? CompilationTimings::Clock::time_point{}
                         : CompilationTimings::Clock::now();
  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  if (timings != nullptr)
    timings->analysis += std::chrono::steady_clock::now() - analysis_start;
  const std::string source_name = source_name_override.empty()
                                      ? source.string()
                                      : std::string{source_name_override};
  if (!analysis.diagnostics.empty())
    std::cerr << janus::diagnostics::render_diagnostics(
        source_name, {}, analysis.diagnostics, diagnostic_format);
  if (warn_high_growth_loops) {
    for (const janus::diagnostics::HighGrowthLoopWarning &warning :
         janus::diagnostics::find_high_growth_loop_warnings(program)) {
      std::cerr << source_name << ':' << warning.location.line << ':'
                << warning.location.column
                << ": warning: high-growth loop pattern may cause integer "
                   "overflow or excessive running time; add an explicit "
                   "bound, use a safe numeric type, or enforce a time budget\n";
    }
  }
  const auto generation_start =
      timings == nullptr ? CompilationTimings::Clock::time_point{}
                         : CompilationTimings::Clock::now();
  janus::backend::llvm::IrGenerator generator{context};
  std::unique_ptr<llvm::Module> module =
      generator.generate(program, source_name, panic_trace, dependencies_only);
  if (llvm::verifyModule(*module, &llvm::errs()))
    throw std::runtime_error{"generated invalid LLVM IR"};
  if (timings != nullptr)
    timings->llvm_generation +=
        std::chrono::steady_clock::now() - generation_start;
  return module;
}

double milliseconds(CompilationTimings::Duration duration) {
  return std::chrono::duration<double, std::milli>{duration}.count();
}

std::string json_escape(std::string_view value) {
  std::ostringstream escaped;
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
      break;
    case '\b':
      escaped << "\\b";
      break;
    case '\f':
      escaped << "\\f";
      break;
    case '\n':
      escaped << "\\n";
      break;
    case '\r':
      escaped << "\\r";
      break;
    case '\t':
      escaped << "\\t";
      break;
    default:
      if (character < 0x20)
        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                << static_cast<unsigned int>(character) << std::dec;
      else
        escaped << character;
    }
  }
  return escaped.str();
}

auto timing_phases(const CompilationTimings &timings) {
  using Entry = std::pair<std::string_view, CompilationTimings::Duration>;
  return std::array<Entry, 7>{{
      {"loading", timings.loading},
      {"parsing", timings.parsing},
      {"analysis", timings.analysis},
      {"llvm_generation", timings.llvm_generation},
      {"optimization", timings.optimization},
      {"link", timings.link},
      {"overhead", timings.overhead},
  }};
}

void print_timings(const CompilationTimings &timings, const Options &options) {
  const auto phases = timing_phases(timings);
  const double total_ms = milliseconds(timings.total);
  std::ostream &output = options.timings_format == Options::TimingsFormat::Json
                             ? std::cout
                             : std::cerr;
  output << std::fixed << std::setprecision(3);
  if (options.timings_format == Options::TimingsFormat::Json) {
    output << "{\"schema_version\":1,\"command\":\"build\",\"unit\":"
              "\"milliseconds\",\"source\":\""
           << json_escape(options.source.generic_string())
           << "\",\"total_ms\":" << total_ms << ",\"phases\":{";
    for (std::size_t index = 0; index < phases.size(); ++index) {
      if (index != 0)
        output << ',';
      output << '"' << phases[index].first << "\":"
             << milliseconds(phases[index].second);
    }
    output << "}}\n";
    return;
  }

  output << "compilation timings (milliseconds):\n";
  for (const auto &[name, duration] : phases) {
    const double value = milliseconds(duration);
    const double percentage = total_ms > 0.0 ? value * 100.0 / total_ms : 0.0;
    output << "  " << std::left << std::setw(18) << name << std::right
           << std::setw(10) << value << " ms  " << std::setprecision(1)
           << std::setw(5) << percentage << "%\n"
           << std::setprecision(3);
  }
  output << "  " << std::left << std::setw(18) << "total" << std::right
         << std::setw(10) << total_ms << " ms  " << std::setprecision(1)
         << std::setw(5) << 100.0 << "%\n";
}

void write_ir(const llvm::Module &module, const std::filesystem::path &path) {
  std::error_code error;
  llvm::raw_fd_ostream output{path.string(), error, llvm::sys::fs::OF_None};
  if (error)
    throw std::runtime_error{"cannot create '" + path.string() +
                             "': " + error.message()};
  module.print(output, nullptr);
}

void write_bitcode(const llvm::Module &module,
                   const std::filesystem::path &path) {
  std::error_code error;
  llvm::raw_fd_ostream output{path.string(), error, llvm::sys::fs::OF_None};
  if (error)
    throw std::runtime_error{"cannot create '" + path.string() +
                             "': " + error.message()};
  llvm::WriteBitcodeToFile(module, output);
}

std::unique_ptr<llvm::Module>
read_bitcode(const std::filesystem::path &path, llvm::LLVMContext &context) {
  auto buffer = llvm::MemoryBuffer::getFile(path.string());
  if (!buffer)
    throw std::runtime_error{"cannot read cached consumer bitcode"};
  auto parsed = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  if (!parsed)
    throw std::runtime_error{"cannot parse cached consumer bitcode"};
  return std::move(*parsed);
}

std::vector<std::string>
consumer_definition_manifest(const llvm::Module &module) {
  std::vector<std::string> definitions;
  for (const llvm::Function &function : module)
    if (!function.isDeclaration() &&
        (!function.hasFnAttribute("janus.module") ||
         function.hasFnAttribute("janus.consumer-owned")))
      definitions.push_back("f:" + function.getName().str());
  for (const llvm::GlobalVariable &global : module.globals())
    if (global.hasInitializer() && global.getMetadata("janus.module") == nullptr)
      definitions.push_back("g:" + global.getName().str());
  std::sort(definitions.begin(), definitions.end());
  definitions.erase(std::unique(definitions.begin(), definitions.end()),
                    definitions.end());
  return definitions;
}

bool has_required_definitions(const llvm::Module &module,
                              const std::vector<std::string> &definitions) {
  return std::all_of(definitions.begin(), definitions.end(),
                     [&](const std::string &definition) {
    if (definition.starts_with("f:")) {
      const llvm::Function *function = module.getFunction(definition.substr(2));
      return function != nullptr && !function->isDeclaration();
    }
    if (definition.starts_with("g:")) {
      const llvm::GlobalVariable *global =
          module.getNamedGlobal(definition.substr(2));
      return global != nullptr && global->hasInitializer();
    }
    return false;
  });
}

bool has_incomplete_consumer_definitions(const llvm::Module &module) {
  return std::any_of(module.begin(), module.end(), [](const llvm::Function &fn) {
    return fn.hasFnAttribute("janus.consumer-owned") && fn.isDeclaration();
  });
}

struct CachedDependencyDefinition {
  std::string name;
  bool global{};
};

std::vector<CachedDependencyDefinition>
discard_cached_dependency_definitions(llvm::Module &module) {
  for (llvm::Function &function : module)
    if (function.hasFnAttribute("janus.module") &&
        !function.hasFnAttribute("janus.consumer-owned") &&
        !function.isDeclaration()) {
      function.addFnAttr("janus.cached-definition");
      function.deleteBody();
    }
  for (llvm::GlobalVariable &global : module.globals())
    if (global.getMetadata("janus.module") != nullptr &&
        global.hasInitializer()) {
      global.setMetadata("janus.cached-definition",
                         llvm::MDNode::get(module.getContext(), {}));
      global.setInitializer(nullptr);
    }
  for (auto iterator = module.global_begin(); iterator != module.global_end();) {
    llvm::GlobalVariable &global = *iterator++;
    if (global.hasLocalLinkage() && global.use_empty())
      global.eraseFromParent();
  }
  for (auto iterator = module.begin(); iterator != module.end();) {
    llvm::Function &function = *iterator++;
    if ((function.hasFnAttribute("janus.module") ||
         function.hasLocalLinkage()) &&
        function.use_empty())
      function.eraseFromParent();
  }
  std::vector<CachedDependencyDefinition> required;
  for (const llvm::Function &function : module)
    if (function.hasFnAttribute("janus.cached-definition") &&
        !function.hasLocalLinkage() &&
        !function.getName().starts_with("__janus_lambda_"))
      required.push_back({function.getName().str(), false});
  for (const llvm::GlobalVariable &global : module.globals())
    if (global.getMetadata("janus.cached-definition") != nullptr &&
        !global.hasLocalLinkage())
      required.push_back({global.getName().str(), true});
  return required;
}

std::filesystem::path default_output(const Options &options) {
  if (!options.output.empty()) {
    std::filesystem::path output = options.output;
#ifdef _WIN32
    if (!options.emit_llvm && !options.emit_object &&
        output.extension() != ".exe")
      output += ".exe";
#endif
    return output;
  }
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

std::filesystem::path snapshot_module_path(
    const std::filesystem::path &root, std::string_view module) {
  if (module.empty())
    throw std::runtime_error{"cannot snapshot an unnamed imported module"};
  std::filesystem::path relative;
  std::size_t start = 0;
  while (start < module.size()) {
    const std::size_t separator = module.find('.', start);
    const std::string_view segment = module.substr(
        start, separator == std::string_view::npos ? module.size() - start
                                                   : separator - start);
    if (segment.empty() ||
        !std::all_of(segment.begin(), segment.end(), [](unsigned char value) {
          return std::isalnum(value) != 0 || value == '_';
        }))
      throw std::runtime_error{"invalid imported module name in build snapshot"};
    relative /= segment;
    if (separator == std::string_view::npos)
      break;
    start = separator + 1;
  }
  relative += ".janus";
  return root / relative;
}

void write_snapshot_source(const std::filesystem::path &path,
                           std::string_view source) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  if (!output)
    throw std::runtime_error{"cannot create build-input snapshot"};
  output << source;
  if (!output)
    throw std::runtime_error{"cannot write build-input snapshot"};
}

std::filesystem::path materialize_build_snapshot(
    const janus::driver::BuildFingerprintInput &inputs,
    const std::filesystem::path &root) {
  const auto entry = root / "entry.janus";
  write_snapshot_source(entry, inputs.source);
  for (const auto &dependency : inputs.dependencies) {
    write_snapshot_source(snapshot_module_path(root, dependency.import_name),
                          dependency.source);
    if (dependency.name != dependency.import_name &&
        dependency.name.find('/') == std::string::npos &&
        dependency.name.find('\\') == std::string::npos)
      write_snapshot_source(snapshot_module_path(root, dependency.name),
                            dependency.source);
  }
  return entry;
}

int build(const Options &options, const std::filesystem::path &output,
          const Toolchain &toolchain, CompilationTimings *timings = nullptr) {
  const auto total_start =
      timings == nullptr ? CompilationTimings::Clock::time_point{}
                         : CompilationTimings::Clock::now();
  if (!output.parent_path().empty())
    std::filesystem::create_directories(output.parent_path());
  std::optional<janus::driver::IncrementalCache> cache;
  std::string cache_key;
  std::string cache_identity;
  std::string consumer_key;
  std::string consumer_cache_identity;
  std::optional<janus::driver::BuildFingerprintInput> fingerprint_inputs;
  const auto inspect_inputs = [&] {
    std::vector<std::filesystem::path> search_paths{toolchain.stdlib};
    search_paths.insert(search_paths.end(), options.dependency_paths.begin(),
                        options.dependency_paths.end());
    std::vector<std::string> compilation_options{
        options.release ? "release" : "debug",
        options.emit_llvm
            ? "emit=llvm-ir"
            : options.emit_object ? "emit=object" : "emit=executable",
        "panic-trace=" +
            std::to_string(static_cast<int>(options.panic_trace)),
        options.warn_high_growth_loops ? "warn-high-growth-loops=on"
                                       : "warn-high-growth-loops=off"};
    return janus::driver::inspect_build_inputs(
        options.source, search_paths, JANUS_VERSION,
        llvm::sys::getDefaultTargetTriple(), std::move(compilation_options));
  };
  if (!options.no_cache && !options.warn_high_growth_loops) {
    const auto inputs = inspect_inputs();
    cache_key = janus::driver::build_fingerprint(inputs);
    cache_identity = janus::driver::build_identity(inputs);
    consumer_key = janus::driver::consumer_fingerprint(inputs);
    consumer_cache_identity = janus::driver::consumer_identity(inputs);
    fingerprint_inputs = inputs;
    const std::filesystem::path cache_root =
        options.manifest.has_value()
            ? options.manifest->root() / "target" / ".janus-cache" / "v1"
            : options.source.parent_path() / ".janus-cache" / "v1";
    cache.emplace(cache_root);
    janus::driver::TemporaryDirectory cached_output =
        janus::driver::TemporaryDirectory::create("janus-cache-hit");
    const auto staged_hit = cached_output.path() / output.filename();
    if (cache->restore(cache_key, cache_identity, staged_hit) ==
        janus::driver::CacheLookup::Hit) {
      const auto current_inputs = inspect_inputs();
      if (janus::driver::build_fingerprint(current_inputs) != cache_key ||
          janus::driver::build_identity(current_inputs) != cache_identity ||
          janus::driver::consumer_fingerprint(current_inputs) != consumer_key ||
          janus::driver::consumer_identity(current_inputs) !=
              consumer_cache_identity)
        throw std::runtime_error{
            "build inputs changed during cache lookup; retry the build"};
      if (cache->restore(cache_key, cache_identity, output) !=
          janus::driver::CacheLookup::Hit)
        throw std::runtime_error{"cached build output changed during restore"};
      if (timings != nullptr) {
        timings->total = std::chrono::steady_clock::now() - total_start;
        timings->overhead = timings->total;
      }
      return 0;
    }
  }
  std::optional<janus::driver::TemporaryDirectory> staged_output_directory;
  std::filesystem::path compilation_output = output;
  if (cache.has_value()) {
    staged_output_directory.emplace(
        janus::driver::TemporaryDirectory::create("janus-cache-output"));
    compilation_output = staged_output_directory->path() / output.filename();
  }
  std::optional<janus::driver::TemporaryDirectory> source_snapshot_directory;
  std::filesystem::path compilation_source = options.source;
  Toolchain compilation_toolchain = toolchain;
  std::vector<std::filesystem::path> compilation_dependency_paths =
      options.dependency_paths;
  if (cache.has_value()) {
    source_snapshot_directory.emplace(
        janus::driver::TemporaryDirectory::create("janus-build-inputs"));
    compilation_source = materialize_build_snapshot(
        *fingerprint_inputs, source_snapshot_directory->path());
    compilation_toolchain.stdlib = source_snapshot_directory->path();
    compilation_dependency_paths.clear();
  }
  llvm::LLVMContext context;
  std::unique_ptr<llvm::Module> module;
  bool reused_consumer = false;
  std::optional<janus::driver::TemporaryDirectory> consumer_directory;
  if (cache.has_value()) {
    consumer_directory.emplace(
        janus::driver::TemporaryDirectory::create("janus-consumer"));
    const auto bitcode = consumer_directory->path() / "consumer.bc";
    std::vector<std::string> required_consumer_definitions;
    if (cache->restore_consumer(consumer_key, consumer_cache_identity, bitcode,
                                &required_consumer_definitions)) {
      try {
        module = read_bitcode(bitcode, context);
      } catch (const std::exception &) {
        cache->invalidate_consumer(consumer_key);
      }
      if (module) {
        const llvm::Function *entry = module->getFunction("main");
        if (entry == nullptr || entry->isDeclaration() ||
            !has_required_definitions(*module,
                                      required_consumer_definitions) ||
            has_incomplete_consumer_definitions(*module) ||
            llvm::verifyModule(*module, &llvm::errs())) {
          cache->invalidate_consumer(consumer_key);
          module.reset();
        }
      }
      if (module) {
        const std::vector<CachedDependencyDefinition> required_definitions =
            discard_cached_dependency_definitions(*module);
        std::unique_ptr<llvm::Module> dependencies =
            compile(compilation_source, context, compilation_toolchain,
                    compilation_dependency_paths,
                    options.warn_high_growth_loops, options.diagnostic_format,
                    options.panic_trace, timings, true,
                    options.source.string());
        if (const llvm::Function *entry = dependencies->getFunction("main");
            entry != nullptr && !entry->isDeclaration())
          throw std::runtime_error{
              "incremental dependency partition contains the consumer entry"};
        llvm::Linker linker{*module};
        const bool link_failed = linker.linkInModule(
            std::move(dependencies), llvm::Linker::Flags::OverrideFromSrc);
        bool incomplete_dependencies = false;
        if (!link_failed) {
          for (const CachedDependencyDefinition &definition :
               required_definitions) {
            if (definition.global) {
              const llvm::GlobalVariable *global =
                  module->getNamedGlobal(definition.name);
              const bool missing =
                  global == nullptr || !global->hasInitializer();
              incomplete_dependencies = incomplete_dependencies || missing;
              if (missing && std::getenv("JANUS_INCREMENTAL_TRACE") != nullptr)
                std::cerr << "incremental: missing dependency global "
                          << definition.name << '\n';
            } else {
              const llvm::Function *function =
                  module->getFunction(definition.name);
              const bool missing =
                  function == nullptr || function->isDeclaration();
              incomplete_dependencies = incomplete_dependencies || missing;
              if (missing && std::getenv("JANUS_INCREMENTAL_TRACE") != nullptr)
                std::cerr << "incremental: missing dependency function "
                          << definition.name << '\n';
            }
          }
        }
        if (link_failed || incomplete_dependencies ||
            !has_required_definitions(*module,
                                      required_consumer_definitions) ||
            has_incomplete_consumer_definitions(*module) ||
            llvm::verifyModule(*module, &llvm::errs()) ||
            module->getFunction("main") == nullptr ||
            module->getFunction("main")->isDeclaration()) {
          cache->invalidate_consumer(consumer_key);
          module.reset();
        } else {
          reused_consumer = true;
        }
      }
    }
  }
  if (!module)
    module = compile(compilation_source, context, compilation_toolchain,
                     compilation_dependency_paths,
                     options.warn_high_growth_loops, options.diagnostic_format,
                     options.panic_trace, timings, false,
                     options.source.string());
  std::optional<std::filesystem::path> pending_consumer_bitcode;
  std::vector<std::string> pending_consumer_definitions;
  if (cache.has_value() && !reused_consumer) {
    const auto bitcode = consumer_directory->path() / "consumer.bc";
    pending_consumer_definitions = consumer_definition_manifest(*module);
    write_bitcode(*module, bitcode);
    pending_consumer_bitcode = bitcode;
  }
  const auto emit_artifact = [&](llvm::Module &current) {
    if (options.emit_llvm) {
      write_ir(current, compilation_output);
      return;
    }
    std::optional<janus::driver::TemporaryDirectory> temporary_directory;
    std::filesystem::path object = compilation_output;
    if (!options.emit_object) {
      temporary_directory.emplace(
          janus::driver::TemporaryDirectory::create("janus-build"));
      object = temporary_directory->path() / "module.o";
    }
    const auto optimization_start =
        timings == nullptr ? CompilationTimings::Clock::time_point{}
                           : CompilationTimings::Clock::now();
    janus::backend::llvm::emit_object(current, object, options.release);
    if (timings != nullptr)
      timings->optimization +=
          std::chrono::steady_clock::now() - optimization_start;
    if (!options.emit_object) {
      const auto link_start =
          timings == nullptr ? CompilationTimings::Clock::time_point{}
                             : CompilationTimings::Clock::now();
      janus::driver::link_executable(
          {object}, compilation_output,
          janus::driver::LinkOptions{!options.release, {toolchain.runtime},
                                     toolchain.clang});
      if (timings != nullptr)
        timings->link += std::chrono::steady_clock::now() - link_start;
    }
  };
  try {
    emit_artifact(*module);
  } catch (const std::exception &) {
    if (!reused_consumer)
      throw;
    cache->invalidate_consumer(consumer_key);
    module = compile(compilation_source, context, compilation_toolchain,
                     compilation_dependency_paths,
                     options.warn_high_growth_loops, options.diagnostic_format,
                     options.panic_trace, timings, false,
                     options.source.string());
    reused_consumer = false;
    const auto bitcode = consumer_directory->path() / "consumer.bc";
    pending_consumer_definitions = consumer_definition_manifest(*module);
    write_bitcode(*module, bitcode);
    pending_consumer_bitcode = bitcode;
    emit_artifact(*module);
  }
  if (const char *trace = std::getenv("JANUS_INCREMENTAL_TRACE");
      trace != nullptr && *trace != '\0')
    std::cerr << "incremental: consumer "
              << (reused_consumer ? "reused" : "compiled") << '\n';
  if (timings != nullptr) {
    timings->total = std::chrono::steady_clock::now() - total_start;
    const auto measured = timings->loading + timings->parsing +
                          timings->analysis + timings->llvm_generation +
                          timings->optimization + timings->link;
    timings->overhead = timings->total > measured
                            ? timings->total - measured
                            : CompilationTimings::Duration{};
  }
  if (cache.has_value()) {
    const auto current_inputs = inspect_inputs();
    if (janus::driver::build_fingerprint(current_inputs) != cache_key ||
        janus::driver::build_identity(current_inputs) != cache_identity ||
        janus::driver::consumer_fingerprint(current_inputs) != consumer_key ||
        janus::driver::consumer_identity(current_inputs) !=
            consumer_cache_identity)
      throw std::runtime_error{
          "build inputs changed during compilation; retry the build"};
    if (pending_consumer_bitcode.has_value())
      cache->store_consumer(consumer_key, consumer_cache_identity,
                            *pending_consumer_bitcode,
                            pending_consumer_definitions);
    cache->store(cache_key, cache_identity, compilation_output, consumer_key);
    if (cache->restore(cache_key, cache_identity, output) !=
        janus::driver::CacheLookup::Hit)
      throw std::runtime_error{"cannot restore newly cached build output"};
  }
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
                                options.diagnostic_format,
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
                      std::string_view{argv[1]} == "search" ||
                      std::string_view{argv[1]} == "publish"))
      return manage_package(argc, argv);
    Options options = parse_options(argc, argv);
    diagnostic_format = options.diagnostic_format;
    if (options.command == "clean") {
      const auto manifest_path =
          janus::driver::find_manifest(std::filesystem::current_path());
      const auto manifest = janus::driver::load_manifest(manifest_path);
      const auto target = manifest.root() / "target";
      std::error_code error;
      std::filesystem::remove_all(target, error);
      if (error)
        throw std::runtime_error{"cannot clean '" + target.string() +
                                 "': " + error.message()};
      std::cout << "removed " << target.string() << '\n';
      return 0;
    }
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
        std::cerr << (options.doc_stdlib ? "error: " : "warning: ")
                  << "unresolved documentation link '[[" << link.symbol
                  << "]]' in " << link.context << '\n';
      for (const janus::driver::DocumentationDiagnostic &diagnostic :
           report.diagnostics)
        std::cerr << (options.doc_stdlib ? "error: " : "warning: ")
                  << diagnostic.symbol << ": " << diagnostic.message << " ["
                  << diagnostic.code << "]\n";
      if (options.doc_stdlib) {
        for (const std::string &module : report.undocumented_modules)
          std::cerr << "error: undocumented standard-library module "
                    << module << '\n';
        for (const std::string &symbol : report.undocumented_symbols)
          std::cerr << "error: undocumented standard-library symbol "
                    << symbol << '\n';
        if (!report.unresolved_links.empty() ||
            !report.undocumented_modules.empty() ||
            !report.undocumented_symbols.empty() ||
            !report.diagnostics.empty())
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
                  options.warn_high_growth_loops, options.diagnostic_format,
                  options.panic_trace));
      std::cout << "checked " << options.source.string() << '\n';
      return 0;
    }
    if (options.command == "build") {
      CompilationTimings timings;
      const int status =
          build(options, default_output(options), toolchain,
                options.timings_format == Options::TimingsFormat::None
                    ? nullptr
                    : &timings);
      if (status == 0 &&
          options.timings_format != Options::TimingsFormat::None)
        print_timings(timings, options);
      return status;
    }

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
