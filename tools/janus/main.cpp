#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/backend/llvm/object_emitter.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/diagnostics/high_growth_loop_linter.hpp"
#include "janus/diagnostics/renderer.hpp"
#include "janus/driver/api_index.hpp"
#include "janus/driver/dependency.hpp"
#include "janus/driver/doctest.hpp"
#include "janus/driver/documentation.hpp"
#include "janus/driver/formatter.hpp"
#include "janus/driver/incremental_cache.hpp"
#include "janus/driver/manifest.hpp"
#include "janus/driver/native_linker.hpp"
#include "janus/driver/native_test.hpp"
#include "janus/driver/project.hpp"
#include "janus/driver/registry.hpp"
#include "janus/driver/temporary_directory.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/semantic/analyzer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

struct Toolchain {
  std::filesystem::path stdlib;
  std::filesystem::path stdlib_api_index;
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
  bool check_all_modules{};
  bool deny_warnings{};
  bool format_check{};
  bool doc_open{};
  bool doc_stdlib{};
  std::string doc_search;
  std::string doc_format{"human"};
  bool doc_format_set{};
  std::string doc_module;
  std::string doc_kind;
  std::string doc_package;
  bool doctests_only{};
  bool test_list{};
  bool test_exact{};
  bool test_ignored{};
  bool test_include_ignored{};
  bool test_fail_fast{};
  bool test_fail_if_empty{};
  enum class TestFormat { Human, Json, Junit } test_format{TestFormat::Human};
  std::size_t test_jobs{1};
  std::chrono::milliseconds test_timeout{};
  bool warn_high_growth_loops{};
  enum class TimingsFormat {
    None,
    Human,
    Json
  } timings_format{TimingsFormat::None};
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
  const std::filesystem::path installed_stdlib_api_index =
      root / "share/doc/janus/stdlib-reference/api-index.json";
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
      std::filesystem::exists(installed_stdlib_api_index)
          ? installed_stdlib_api_index
          : std::filesystem::path{JANUS_STDLIB_DIR}.parent_path() /
                "website/docs/reference/stdlib/api-index.json",
      std::filesystem::exists(installed_runtime)
          ? installed_runtime
          : std::filesystem::path{JANUS_RUNTIME_LIBRARY},
      std::filesystem::exists(bundled_clang)
          ? bundled_clang
          : std::filesystem::path{JANUS_CLANG_PATH},
  };
}

janus::driver::ApiIndex load_index_or_sources(
    const std::filesystem::path &index_path,
    const std::vector<std::filesystem::path> &source_roots,
    const janus::driver::ApiIndexMetadata &metadata) {
  if (std::filesystem::is_regular_file(index_path))
    return janus::driver::load_api_index(index_path);
  return janus::driver::build_api_index_from_source_roots(source_roots,
                                                           metadata);
}

std::filesystem::path
first_api_index(const std::vector<std::filesystem::path> &candidates) {
  const auto found = std::find_if(candidates.begin(), candidates.end(),
                                  [](const auto &candidate) {
                                    return std::filesystem::is_regular_file(
                                        candidate);
                                  });
  return found == candidates.end() ? std::filesystem::path{} : *found;
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
            "[--all] [--deny-warnings] "
            "[--diagnostic-format human|json]\n"
         << "  janus build [source.janus] [-o output] [--release] "
            "[--emit llvm-ir|object] [--panic-trace full|short|off] "
            "[--diagnostic-format human|json] [--timings[=human|json]] "
            "[--no-cache] [--deny-warnings]\n"
         << "  janus run [source.janus] [--release] "
            "[--panic-trace full|short|off]\n"
         << "  janus test [filter] [--doc] [--doc-path <path>] "
            "[--list] [--exact] [--ignored|--include-ignored] "
            "[--jobs <count>] [--timeout <duration>] [--fail-fast] "
            "[--fail-if-empty] [--format human|json|junit] [--release] "
            "[--panic-trace full|short|off]\n"
         << "  janus fmt [source.janus] [--check]\n"
         << "  janus doc [--stdlib] [-o directory] [--open] [--offline] "
            "[--search QUERY] [--format human|json] "
            "[--module NAME] [--kind KIND] [--package NAME]\n"
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
              "[--all] [--deny-warnings] [--warn-high-growth-loops] "
              "[--diagnostic-format human|json]\n";
  else if (command == "build")
    output << " [source.janus] [-o output] [--release] "
              "[--emit llvm-ir|object] [--locked] [--offline] "
              "[--panic-trace full|short|off] "
              "[--warn-high-growth-loops] "
              "[--diagnostic-format human|json] "
              "[--timings[=human|json]] [--no-cache] [--deny-warnings]\n";
  else if (command == "run")
    output << " [source.janus] [--release] [--locked] [--offline] "
              "[--panic-trace full|short|off] [--warn-high-growth-loops]\n";
  else if (command == "test")
    output << " [filter] [--doc] [--doc-path <path>] [--list] [--exact] "
              "[--ignored|--include-ignored] [--jobs <count>] "
              "[--timeout <duration>] [--fail-fast] [--fail-if-empty] "
              "[--format human|json|junit] [--release] "
              "[--locked] [--offline] "
              "[--panic-trace full|short|off]\n";
  else if (command == "doc")
    output << " [--stdlib] [-o directory] [--open] [--offline] "
              "[--search QUERY] [--format human|json] "
              "[--module NAME] [--kind KIND] [--package NAME]\n";
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

std::string render_compile_error(const std::filesystem::path &path,
                                 std::string_view source,
                                 const janus::CompileError &error) {
  std::size_t width = 80;
  if (const char *columns = std::getenv("COLUMNS")) {
    const unsigned long parsed = std::strtoul(columns, nullptr, 10);
    if (parsed >= 20)
      width = parsed;
  }
  return janus::diagnostics::render_diagnostics(
      path, source, error.diagnostics(),
      janus::diagnostics::DiagnosticFormat::Human, {width});
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
    } else if (argument == "--all") {
      options.check_all_modules = true;
    } else if (argument == "--deny-warnings") {
      options.deny_warnings = true;
    } else if (argument == "--check" && options.command == "fmt") {
      options.format_check = true;
    } else if (argument == "--open" && options.command == "doc") {
      options.doc_open = true;
    } else if (argument == "--stdlib" && options.command == "doc") {
      options.doc_stdlib = true;
    } else if (argument == "--search" && options.command == "doc") {
      if (++index == argc)
        throw UsageError{options.command, "--search requires a query"};
      options.doc_search = argv[index];
    } else if (argument == "--module" && options.command == "doc") {
      if (++index == argc)
        throw UsageError{options.command, "--module requires a name"};
      options.doc_module = argv[index];
    } else if (argument == "--kind" && options.command == "doc") {
      if (++index == argc)
        throw UsageError{options.command, "--kind requires a kind"};
      options.doc_kind = argv[index];
    } else if (argument == "--package" && options.command == "doc") {
      if (++index == argc)
        throw UsageError{options.command, "--package requires a name"};
      options.doc_package = argv[index];
    } else if (argument == "--format" && options.command == "doc") {
      if (++index == argc || (std::string_view{argv[index]} != "human" &&
                             std::string_view{argv[index]} != "json"))
        throw UsageError{options.command,
                         "--format requires human or json"};
      options.doc_format = argv[index];
      options.doc_format_set = true;
    } else if (argument == "--doc" && options.command == "test") {
      options.doctests_only = true;
    } else if (argument == "--list" && options.command == "test") {
      options.test_list = true;
    } else if (argument == "--exact" && options.command == "test") {
      options.test_exact = true;
    } else if (argument == "--ignored" && options.command == "test") {
      options.test_ignored = true;
    } else if (argument == "--include-ignored" && options.command == "test") {
      options.test_include_ignored = true;
    } else if (argument == "--fail-fast" && options.command == "test") {
      options.test_fail_fast = true;
    } else if (argument == "--fail-if-empty" && options.command == "test") {
      options.test_fail_if_empty = true;
    } else if (argument == "--jobs" && options.command == "test") {
      if (++index == argc)
        throw UsageError{options.command, "--jobs requires a positive count"};
      std::string_view value = argv[index];
      std::size_t parsed{};
      const auto [end, error] =
          std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (error != std::errc{} || end != value.data() + value.size() ||
          parsed == 0)
        throw UsageError{options.command, "--jobs requires a positive count"};
      options.test_jobs = parsed;
    } else if (argument == "--timeout" && options.command == "test") {
      if (++index == argc)
        throw UsageError{options.command, "--timeout requires a duration"};
      std::string_view value = argv[index];
      std::chrono::milliseconds multiplier{1000};
      if (value.ends_with("ms")) {
        multiplier = std::chrono::milliseconds{1};
        value.remove_suffix(2);
      } else if (value.ends_with('s')) {
        value.remove_suffix(1);
      } else if (value.ends_with('m')) {
        multiplier = std::chrono::minutes{1};
        value.remove_suffix(1);
      }
      std::uint64_t parsed{};
      const auto [end, error] =
          std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (error != std::errc{} || end != value.data() + value.size() ||
          parsed == 0 ||
          parsed > static_cast<std::uint64_t>(
                       std::chrono::milliseconds::max().count() /
                       multiplier.count()))
        throw UsageError{options.command,
                         "--timeout requires a positive duration (ms, s, m)"};
      options.test_timeout = multiplier * parsed;
    } else if (argument == "--format" && options.command == "test") {
      if (++index == argc)
        throw UsageError{options.command,
                         "--format requires human, json, or junit"};
      const std::string_view format = argv[index];
      if (format == "human")
        options.test_format = Options::TestFormat::Human;
      else if (format == "json")
        options.test_format = Options::TestFormat::Json;
      else if (format == "junit")
        options.test_format = Options::TestFormat::Junit;
      else
        throw UsageError{options.command,
                         "--format accepts 'human', 'json', or 'junit'"};
    } else if (argument == "--doc-path" && options.command == "test") {
      if (++index == argc)
        throw UsageError{options.command,
                         "--doc-path requires a relative path"};
      options.documentation_paths.emplace_back(argv[index]);
    } else if (argument == "--warn-high-growth-loops") {
      options.warn_high_growth_loops = true;
    } else if (argument == "--timings" || argument.starts_with("--timings=")) {
      if (options.timings_format != Options::TimingsFormat::None)
        throw UsageError{options.command,
                         "--timings may be specified only once"};
      const std::string_view format = argument == "--timings"
                                          ? std::string_view{"human"}
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
  if (options.test_ignored && options.test_include_ignored)
    throw UsageError{options.command,
                     "--ignored and --include-ignored are mutually exclusive"};
  if (options.test_exact && options.test_filter.empty())
    throw UsageError{options.command, "--exact requires a filter"};
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
                     "doc only accepts --stdlib, -o, --open, --offline, "
                     "--search, --format, --module, --kind, and --package"};
  if (options.command == "doc" && options.doc_search.empty() &&
      (!options.doc_module.empty() || !options.doc_kind.empty() ||
       !options.doc_package.empty() || options.doc_format_set))
    throw UsageError{
        options.command,
        "--module, --kind, --package, and --format require --search"};
  if (options.command == "doc" && !options.doc_search.empty() &&
      (!options.output.empty() || options.doc_open))
    throw UsageError{options.command,
                     "--search does not accept -o or --open"};
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
    throw UsageError{options.command, "--timings is only available for build"};
  if (options.no_cache && options.command != "build")
    throw UsageError{options.command, "--no-cache is only available for build"};
  if (options.check_all_modules && options.command != "check")
    throw UsageError{options.command, "--all is only available for check"};
  if (options.deny_warnings && options.command != "check" &&
      options.command != "build")
    throw UsageError{options.command,
                     "--deny-warnings is only available for check and build"};
  if (options.command == "clean" &&
      (!options.source.empty() || !options.output.empty() || options.release ||
       options.locked || options.offline || options.no_cache ||
       options.format_check || options.doc_open || options.doc_stdlib ||
       options.doctests_only || options.test_list || options.test_exact ||
       options.test_ignored || options.test_include_ignored ||
       options.test_fail_fast || options.test_fail_if_empty ||
       options.test_jobs != 1 || options.test_timeout.count() != 0 ||
       options.test_format != Options::TestFormat::Human ||
       options.warn_high_growth_loops ||
       options.timings_format != Options::TimingsFormat::None ||
       options.diagnostic_format_set || options.panic_trace_set ||
       options.check_all_modules || options.deny_warnings ||
       options.emit_llvm || options.emit_object))
    throw UsageError{options.command, "clean does not accept arguments"};
  if (options.release && !options.panic_trace_set)
    options.panic_trace = janus::backend::llvm::PanicTraceMode::Short;
  if (options.source.empty() && !options.doc_stdlib &&
      !(options.command == "doc" && !options.doc_search.empty()) &&
      options.command != "clean") {
    options.manifest = janus::driver::load_manifest(
        janus::driver::find_manifest(std::filesystem::current_path()));
    options.source = options.manifest->entry_path();
  } else if (!options.source.empty() &&
             (options.command == "check" || options.command == "build" ||
              options.command == "run")) {
    try {
      options.manifest = janus::driver::load_manifest(
          janus::driver::find_manifest(options.source));
    } catch (const std::runtime_error &) {
    }
  } else if (options.source.empty() && options.command == "doc" &&
             !options.doc_search.empty()) {
    try {
      options.manifest = janus::driver::load_manifest(
          janus::driver::find_manifest(std::filesystem::current_path()));
      options.source = options.manifest->entry_path();
    } catch (const std::runtime_error &) {
    }
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
        CompilationTimings *timings = nullptr, bool dependencies_only = false,
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
  const auto analysis_start = timings == nullptr
                                  ? CompilationTimings::Clock::time_point{}
                                  : CompilationTimings::Clock::now();
  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(
      program,
      janus::semantic::AnalysisOptions{!program.module_name.has_value()});
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
  const auto generation_start = timings == nullptr
                                    ? CompilationTimings::Clock::time_point{}
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

struct DiagnosticBatch {
  std::filesystem::path path;
  std::string source;
  std::vector<janus::Diagnostic> diagnostics;
};

std::vector<std::filesystem::path> project_sources(const Options &options,
                                                   bool exhaustive) {
  std::vector<std::filesystem::path> paths;
  const auto add = [&paths](const std::filesystem::path &path) {
    const std::filesystem::path normalized =
        std::filesystem::absolute(path).lexically_normal();
    if (std::filesystem::is_regular_file(normalized) &&
        std::find(paths.begin(), paths.end(), normalized) == paths.end())
      paths.push_back(normalized);
  };
  add(options.source);
  if (exhaustive && options.manifest.has_value()) {
    const std::filesystem::path source_root = options.manifest->root() / "src";
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator
             iterator{source_root,
                      std::filesystem::directory_options::
                          skip_permission_denied,
                      error},
         end;
         iterator != end; iterator.increment(error)) {
      if (error) {
        error.clear();
        continue;
      }
      if (iterator->is_regular_file(error) &&
          iterator->path().extension() == ".janus")
        add(iterator->path());
      error.clear();
    }
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

int check_sources(const Options &options, const Toolchain &toolchain,
                  bool exhaustive) {
  std::vector<DiagnosticBatch> batches;
  bool has_error = false;
  bool has_warning = false;
  for (const std::filesystem::path &path :
       project_sources(options, exhaustive)) {
    DiagnosticBatch batch;
    batch.path = path;
    try {
      std::ifstream input{path, std::ios::binary};
      if (!input)
        throw std::runtime_error{"cannot read '" + path.string() + "'"};
      batch.source.assign(std::istreambuf_iterator<char>{input},
                          std::istreambuf_iterator<char>{});
      std::vector<std::filesystem::path> search_paths{toolchain.stdlib};
      search_paths.insert(search_paths.end(), options.dependency_paths.begin(),
                          options.dependency_paths.end());
      janus::frontend::ModuleLoader loader{std::move(search_paths)};
      const janus::ast::Program program = loader.load(path);
      const janus::semantic::AnalysisResult analysis =
          janus::semantic::Analyzer{}.analyze(
              program, janus::semantic::AnalysisOptions{
                           !program.module_name.has_value()});
      batch.diagnostics = analysis.diagnostics;
      if (options.warn_high_growth_loops)
        for (const janus::diagnostics::HighGrowthLoopWarning &warning :
             janus::diagnostics::find_high_growth_loop_warnings(program))
          batch.diagnostics.push_back(janus::Diagnostic{
              janus::DiagnosticSeverity::Warning,
              janus::DiagnosticCode::Unclassified,
              "high-growth loop pattern may cause integer overflow or "
              "excessive "
              "running time; add an explicit bound, use a safe numeric type, "
              "or "
              "enforce a time budget",
              warning.location,
              {},
              {},
              {},
          });
    } catch (const janus::CompileError &error) {
      batch.diagnostics = error.diagnostics();
    } catch (const std::exception &error) {
      batch.diagnostics.push_back(janus::Diagnostic{
          janus::DiagnosticSeverity::Error,
          janus::DiagnosticCode::Unclassified,
          error.what(),
          janus::SourceLocation{},
          {},
          {},
          {},
      });
    }
    for (janus::Diagnostic &diagnostic : batch.diagnostics) {
      if (diagnostic.source_path.empty())
        diagnostic.source_path = path;
      has_error =
          has_error || diagnostic.severity == janus::DiagnosticSeverity::Error;
      has_warning = has_warning ||
                    diagnostic.severity == janus::DiagnosticSeverity::Warning;
    }
    if (!batch.diagnostics.empty())
      batches.push_back(std::move(batch));
  }

  std::vector<DiagnosticBatch> consolidated;
  for (DiagnosticBatch &batch : batches)
    for (janus::Diagnostic &diagnostic : batch.diagnostics) {
      const std::filesystem::path path = diagnostic.source_path;
      auto target = std::find_if(consolidated.begin(), consolidated.end(),
                                 [&path](const DiagnosticBatch &candidate) {
                                   return candidate.path == path;
                                 });
      if (target == consolidated.end()) {
        DiagnosticBatch located;
        located.path = path;
        if (path == batch.path) {
          located.source = batch.source;
        } else {
          std::ifstream input{path, std::ios::binary};
          located.source.assign(std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{});
        }
        consolidated.push_back(std::move(located));
        target = std::prev(consolidated.end());
      }
      const bool duplicate =
          std::any_of(target->diagnostics.begin(), target->diagnostics.end(),
                      [&diagnostic](const janus::Diagnostic &candidate) {
                        return candidate.severity == diagnostic.severity &&
                               candidate.code == diagnostic.code &&
                               candidate.message == diagnostic.message &&
                               candidate.primary_location.offset ==
                                   diagnostic.primary_location.offset &&
                               candidate.primary_location.line ==
                                   diagnostic.primary_location.line &&
                               candidate.primary_location.column ==
                                   diagnostic.primary_location.column;
                      });
      if (!duplicate)
        target->diagnostics.push_back(std::move(diagnostic));
    }
  batches = std::move(consolidated);
  std::sort(batches.begin(), batches.end(),
            [](const DiagnosticBatch &left, const DiagnosticBatch &right) {
              return left.path < right.path;
            });

  if (options.diagnostic_format == janus::diagnostics::DiagnosticFormat::Json) {
    std::vector<janus::Diagnostic> diagnostics;
    for (DiagnosticBatch &batch : batches)
      diagnostics.insert(diagnostics.end(),
                         std::make_move_iterator(batch.diagnostics.begin()),
                         std::make_move_iterator(batch.diagnostics.end()));
    if (!diagnostics.empty())
      std::cerr << janus::diagnostics::render_diagnostics(
          {}, {}, diagnostics, options.diagnostic_format);
  } else {
    for (const DiagnosticBatch &batch : batches)
      std::cerr << janus::diagnostics::render_diagnostics(
          batch.path, batch.source, batch.diagnostics,
          options.diagnostic_format);
  }
  return has_error || (options.deny_warnings && has_warning) ? 1 : 0;
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
      output << '"' << phases[index].first
             << "\":" << milliseconds(phases[index].second);
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

std::unique_ptr<llvm::Module> read_bitcode(const std::filesystem::path &path,
                                           llvm::LLVMContext &context) {
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
    if (global.hasInitializer() &&
        global.getMetadata("janus.module") == nullptr)
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
                         const llvm::Function *function =
                             module.getFunction(definition.substr(2));
                         return function != nullptr &&
                                !function->isDeclaration();
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
  return std::any_of(
      module.begin(), module.end(), [](const llvm::Function &fn) {
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
  for (auto iterator = module.global_begin();
       iterator != module.global_end();) {
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

std::filesystem::path snapshot_module_path(const std::filesystem::path &root,
                                           std::string_view module) {
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
      throw std::runtime_error{
          "invalid imported module name in build snapshot"};
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

std::filesystem::path
materialize_build_snapshot(const janus::driver::BuildFingerprintInput &inputs,
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
  const auto total_start = timings == nullptr
                               ? CompilationTimings::Clock::time_point{}
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
        options.emit_llvm     ? "emit=llvm-ir"
        : options.emit_object ? "emit=object"
                              : "emit=executable",
        "panic-trace=" + std::to_string(static_cast<int>(options.panic_trace)),
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
            !has_required_definitions(*module, required_consumer_definitions) ||
            has_incomplete_consumer_definitions(*module) ||
            llvm::verifyModule(*module, &llvm::errs())) {
          cache->invalidate_consumer(consumer_key);
          module.reset();
        }
      }
      if (module) {
        const std::vector<CachedDependencyDefinition> required_definitions =
            discard_cached_dependency_definitions(*module);
        std::unique_ptr<llvm::Module> dependencies = compile(
            compilation_source, context, compilation_toolchain,
            compilation_dependency_paths, options.warn_high_growth_loops,
            options.diagnostic_format, options.panic_trace, timings, true,
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
            !has_required_definitions(*module, required_consumer_definitions) ||
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
    module =
        compile(compilation_source, context, compilation_toolchain,
                compilation_dependency_paths, options.warn_high_growth_loops,
                options.diagnostic_format, options.panic_trace, timings, false,
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
      const auto link_start = timings == nullptr
                                  ? CompilationTimings::Clock::time_point{}
                                  : CompilationTimings::Clock::now();
      janus::driver::link_executable(
          {object}, compilation_output,
          janus::driver::LinkOptions{
              !options.release, {toolchain.runtime}, toolchain.clang});
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
    module =
        compile(compilation_source, context, compilation_toolchain,
                compilation_dependency_paths, options.warn_high_growth_loops,
                options.diagnostic_format, options.panic_trace, timings, false,
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

struct ChildResult {
  int exit_code{};
  bool signaled{};
  bool timed_out{};
  std::string standard_output;
  std::string standard_error;
};

#ifndef _WIN32
void drain_pipe(int descriptor, std::string &output) {
  std::array<char, 4096> buffer{};
  while (true) {
    const ssize_t count = read(descriptor, buffer.data(), buffer.size());
    if (count > 0)
      output.append(buffer.data(), static_cast<std::size_t>(count));
    else
      break;
  }
}
#endif

ChildResult run_child(const std::filesystem::path &executable,
                      const std::filesystem::path &working_directory,
                      std::chrono::milliseconds timeout) {
#ifdef _WIN32
  janus::driver::TemporaryDirectory directory =
      janus::driver::TemporaryDirectory::create("janus-test-output");
  const auto out = directory.path() / "stdout.txt";
  const auto err = directory.path() / "stderr.txt";
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  const HANDLE stdout_handle =
      CreateFileW(out.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (stdout_handle == INVALID_HANDLE_VALUE)
    throw std::system_error{static_cast<int>(GetLastError()),
                            std::system_category(),
                            "cannot create test stdout capture"};
  const HANDLE stderr_handle =
      CreateFileW(err.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (stderr_handle == INVALID_HANDLE_VALUE) {
    CloseHandle(stdout_handle);
    throw std::system_error{static_cast<int>(GetLastError()),
                            std::system_category(),
                            "cannot create test stderr capture"};
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = stdout_handle;
  startup.hStdError = stderr_handle;
  PROCESS_INFORMATION process{};
  std::wstring command_line = L"\"" + executable.wstring() + L"\"";
  const BOOL created = CreateProcessW(
      executable.c_str(), command_line.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW, nullptr, working_directory.c_str(), &startup, &process);
  const DWORD create_error = created ? ERROR_SUCCESS : GetLastError();
  CloseHandle(stdout_handle);
  CloseHandle(stderr_handle);
  if (!created)
    throw std::system_error{static_cast<int>(create_error),
                            std::system_category(),
                            "cannot create test process"};
  CloseHandle(process.hThread);
  const DWORD wait_timeout =
      timeout.count() == 0
          ? INFINITE
          : static_cast<DWORD>(std::min<std::int64_t>(
                timeout.count(), static_cast<std::int64_t>(INFINITE - 1)));
  const DWORD wait_status = WaitForSingleObject(process.hProcess, wait_timeout);
  const bool timed_out = wait_status == WAIT_TIMEOUT;
  if (timed_out) {
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, INFINITE);
  } else if (wait_status == WAIT_FAILED) {
    const DWORD wait_error = GetLastError();
    CloseHandle(process.hProcess);
    throw std::system_error{static_cast<int>(wait_error),
                            std::system_category(),
                            "cannot wait for test process"};
  }
  DWORD exit_code = 1;
  if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
    const DWORD exit_error = GetLastError();
    CloseHandle(process.hProcess);
    throw std::system_error{static_cast<int>(exit_error),
                            std::system_category(),
                            "cannot read test process status"};
  }
  CloseHandle(process.hProcess);
  const auto read_output = [](const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
  };
  return {static_cast<int>(exit_code), false, timed_out, read_output(out),
          read_output(err)};
#else
  int stdout_pipe[2]{};
  int stderr_pipe[2]{};
  if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
    throw std::runtime_error{"cannot create test output pipes"};
  const pid_t child = fork();
  if (child < 0)
    throw std::runtime_error{"cannot create test process"};
  if (child == 0) {
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    if (chdir(working_directory.c_str()) != 0)
      _exit(126);
    execl(executable.c_str(), executable.filename().c_str(),
          static_cast<char *>(nullptr));
    _exit(127);
  }
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);
  fcntl(stdout_pipe[0], F_SETFL, fcntl(stdout_pipe[0], F_GETFL) | O_NONBLOCK);
  fcntl(stderr_pipe[0], F_SETFL, fcntl(stderr_pipe[0], F_GETFL) | O_NONBLOCK);
  ChildResult result;
  int status{};
  const auto start = std::chrono::steady_clock::now();
  while (waitpid(child, &status, WNOHANG) == 0) {
    drain_pipe(stdout_pipe[0], result.standard_output);
    drain_pipe(stderr_pipe[0], result.standard_error);
    if (timeout.count() != 0 &&
        std::chrono::steady_clock::now() - start >= timeout) {
      result.timed_out = true;
      kill(child, SIGKILL);
      static_cast<void>(waitpid(child, &status, 0));
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  drain_pipe(stdout_pipe[0], result.standard_output);
  drain_pipe(stderr_pipe[0], result.standard_error);
  close(stdout_pipe[0]);
  close(stderr_pipe[0]);
  result.signaled = WIFSIGNALED(status);
  result.exit_code =
      WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  return result;
#endif
}

std::string escape_json(std::string_view value) {
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

std::string escape_xml(std::string_view value) {
  std::string escaped;
  for (const char character : value) {
    if (character == '&')
      escaped += "&amp;";
    else if (character == '<')
      escaped += "&lt;";
    else if (character == '>')
      escaped += "&gt;";
    else if (character == '"')
      escaped += "&quot;";
    else if (character == '\'')
      escaped += "&apos;";
    else
      escaped += character;
  }
  return escaped;
}

enum class TestStatus { Passed, Failed, Ignored, TimedOut, Crashed, Error };

std::string_view status_name(TestStatus status) {
  switch (status) {
  case TestStatus::Passed:
    return "passed";
  case TestStatus::Failed:
    return "failed";
  case TestStatus::Ignored:
    return "ignored";
  case TestStatus::TimedOut:
    return "timed_out";
  case TestStatus::Crashed:
    return "crashed";
  case TestStatus::Error:
    return "error";
  }
  return "error";
}

struct TestResult {
  std::string identifier;
  std::string kind{"unit"};
  std::filesystem::path source;
  janus::SourceLocation location{};
  TestStatus status{TestStatus::Error};
  std::chrono::milliseconds duration{};
  std::string message;
  std::string standard_output;
  std::string standard_error;
};

void print_test_report(const std::vector<TestResult> &results,
                       const Options &options) {
  const auto count = [&results](TestStatus status) {
    return std::count_if(
        results.begin(), results.end(),
        [status](const TestResult &result) { return result.status == status; });
  };
  const std::size_t passed = count(TestStatus::Passed);
  const std::size_t ignored = count(TestStatus::Ignored);
  const std::size_t failed = results.size() - passed - ignored;
  const std::size_t unit = std::count_if(
      results.begin(), results.end(),
      [](const TestResult &result) { return result.kind == "unit"; });
  const std::size_t doctest = results.size() - unit;

  if (options.test_format == Options::TestFormat::Json) {
    std::cout << "{\"schema_version\":1,\"summary\":{\"passed\":" << passed
              << ",\"failed\":" << failed << ",\"ignored\":" << ignored
              << ",\"unit\":" << unit << ",\"doctest\":" << doctest
              << "},\"tests\":[";
    for (std::size_t index = 0; index < results.size(); ++index) {
      const TestResult &result = results[index];
      if (index != 0)
        std::cout << ',';
      std::cout << "{\"id\":\"" << escape_json(result.identifier)
                << "\",\"kind\":\"" << result.kind << "\",\"file\":\""
                << escape_json(result.source.generic_string())
                << "\",\"line\":" << result.location.line
                << ",\"column\":" << result.location.column << ",\"status\":\""
                << status_name(result.status)
                << "\",\"duration_ms\":" << result.duration.count()
                << ",\"message\":\"" << escape_json(result.message)
                << "\",\"stdout\":\"" << escape_json(result.standard_output)
                << "\",\"stderr\":\"" << escape_json(result.standard_error)
                << "\"}";
    }
    std::cout << "]}\n";
    return;
  }
  if (options.test_format == Options::TestFormat::Junit) {
    std::chrono::milliseconds duration{};
    for (const TestResult &result : results)
      duration += result.duration;
    std::cout << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              << "<testsuite name=\"janus\" tests=\"" << results.size()
              << "\" failures=\"" << failed << "\" skipped=\"" << ignored
              << "\" time=\"" << std::fixed << std::setprecision(3)
              << duration.count() / 1000.0 << "\">\n";
    for (const TestResult &result : results) {
      std::cout << "  <testcase classname=\"" << result.kind << "\" name=\""
                << escape_xml(result.identifier) << "\" file=\""
                << escape_xml(result.source.generic_string()) << "\" line=\""
                << result.location.line << "\" time=\""
                << result.duration.count() / 1000.0 << "\">";
      if (result.status == TestStatus::Ignored)
        std::cout << "<skipped/>";
      else if (result.status != TestStatus::Passed)
        std::cout << "<failure type=\"" << status_name(result.status)
                  << "\" message=\"" << escape_xml(result.message) << "\">"
                  << escape_xml(result.standard_error) << "</failure>";
      if (!result.standard_output.empty())
        std::cout << "<system-out>" << escape_xml(result.standard_output)
                  << "</system-out>";
      if (!result.standard_error.empty())
        std::cout << "<system-err>" << escape_xml(result.standard_error)
                  << "</system-err>";
      std::cout << "</testcase>\n";
    }
    std::cout << "</testsuite>\n";
    return;
  }
  for (const TestResult &result : results) {
    std::cout << (result.kind == "doctest" ? "doctest " : "test ")
              << result.identifier << " ... ";
    if (result.status == TestStatus::Passed)
      std::cout << "ok";
    else if (result.status == TestStatus::Ignored)
      std::cout << "ignored";
    else
      std::cout << "FAILED (" << status_name(result.status) << ')';
    std::cout << " [" << result.duration.count() << " ms]\n";
    if (result.status != TestStatus::Passed &&
        result.status != TestStatus::Ignored) {
      if (!result.message.empty())
        std::cerr << result.message << '\n';
      if (!result.standard_output.empty())
        std::cerr << "--- stdout ---\n" << result.standard_output;
      if (!result.standard_error.empty())
        std::cerr << "--- stderr ---\n" << result.standard_error;
    }
  }
  std::cout << "\ntest result: " << (failed == 0 ? "ok" : "FAILED") << ". "
            << passed << " passed; " << failed << " failed; " << ignored
            << " ignored (" << unit << " unit; " << doctest << " doctest)\n";
}

int run_tests(const Options &options, const Toolchain &toolchain) {
  const std::filesystem::path root = options.manifest->root();
  const std::filesystem::path tests_root = root / "tests";
  std::vector<janus::driver::NativeTest> tests;
  if (!options.doctests_only)
    tests = janus::driver::discover_native_tests(tests_root);
  std::erase_if(tests, [&options](const janus::driver::NativeTest &test) {
    if (!janus::driver::matches_native_test_filter(test, options.test_filter,
                                                   options.test_exact))
      return true;
    if (options.test_ignored)
      return !test.ignored;
    return false;
  });

  std::vector<std::filesystem::path> documentation_paths =
      options.documentation_paths;
  if (documentation_paths.empty())
    documentation_paths = {"README.md", "docs"};
  std::vector<janus::driver::Doctest> doctests =
      janus::driver::discover_doctests(root, documentation_paths);
  std::erase_if(doctests, [&options](const janus::driver::Doctest &test) {
    if (options.test_ignored)
      return true;
    if (options.test_exact)
      return test.display_name() != options.test_filter;
    return !janus::driver::matches_doctest_filter(test, options.test_filter);
  });

  if (options.test_list) {
    for (const auto &test : tests)
      std::cout << test.identifier << ": unit"
                << (test.ignored ? " (ignored)" : "") << '\n';
    for (const auto &test : doctests)
      std::cout << test.display_name() << ": doctest\n";
    std::cout << tests.size() + doctests.size() << " tests\n";
    return tests.empty() && doctests.empty() && options.test_fail_if_empty ? 4
                                                                           : 0;
  }
  if (tests.empty() && doctests.empty()) {
    if (options.test_format == Options::TestFormat::Human)
      std::cerr << "warning: no tests discovered\n";
    print_test_report({}, options);
    return options.test_fail_if_empty ? 4 : 0;
  }

  janus::driver::TemporaryDirectory generated_directory =
      janus::driver::TemporaryDirectory::create("janus-native-tests");
  struct Prepared {
    janus::driver::NativeTest test;
    std::filesystem::path executable;
    std::optional<TestResult> result;
  };
  std::vector<Prepared> prepared;
  prepared.reserve(tests.size());
  for (std::size_t index = 0; index < tests.size(); ++index) {
    const auto &test = tests[index];
    std::string safe = test.identifier;
    std::replace_if(
        safe.begin(), safe.end(),
        [](char value) {
          return !std::isalnum(static_cast<unsigned char>(value));
        },
        '_');
    std::filesystem::path executable = root / "target" /
                                       (options.release ? "release" : "debug") /
                                       "tests" / safe;
#ifdef _WIN32
    executable += ".exe";
#endif
    Prepared item{test, executable, std::nullopt};
    if (test.ignored && !options.test_ignored &&
        !options.test_include_ignored) {
      item.result = TestResult{test.identifier,
                               "unit",
                               test.source,
                               test.location,
                               TestStatus::Ignored,
                               std::chrono::milliseconds{},
                               {},
                               {},
                               {}};
      prepared.push_back(std::move(item));
      continue;
    }
    std::string original;
    try {
      std::ifstream input{test.source, std::ios::binary};
      original = {std::istreambuf_iterator<char>{input},
                  std::istreambuf_iterator<char>{}};
      const std::filesystem::path generated =
          generated_directory.path() /
          ("test-" + std::to_string(index) + ".janus");
      std::ofstream output{generated, std::ios::binary};
      if (!output)
        throw std::runtime_error{"cannot create generated test"};
      output << janus::driver::native_test_source(original, test);
      output.close();
      Options test_options = options;
      test_options.source = generated;
      test_options.warn_high_growth_loops = false;
      test_options.dependency_paths.push_back(test.source.parent_path());
      build(test_options, executable, toolchain);
    } catch (const janus::CompileError &error) {
      item.result =
          TestResult{test.identifier,
                     "unit",
                     test.source,
                     test.location,
                     TestStatus::Error,
                     std::chrono::milliseconds{},
                     "test compilation failed",
                     {},
                     render_compile_error(test.source, original, error)};
    } catch (const std::exception &error) {
      item.result = TestResult{test.identifier,
                               "unit",
                               test.source,
                               test.location,
                               TestStatus::Error,
                               std::chrono::milliseconds{},
                               error.what(),
                               {},
                               {}};
    }
    prepared.push_back(std::move(item));
    if (options.test_fail_fast && prepared.back().result.has_value())
      break;
  }

  const auto execute = [&](Prepared &item) {
    if (item.result.has_value())
      return;
    const auto start = std::chrono::steady_clock::now();
    const ChildResult child =
        run_child(item.executable, root, options.test_timeout);
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    TestStatus status = TestStatus::Failed;
    std::string message;
    if (child.timed_out) {
      status = TestStatus::TimedOut;
      message = "test exceeded timeout of " +
                std::to_string(options.test_timeout.count()) + " ms";
    } else if (item.test.expected_panic.has_value()) {
      const std::string combined = child.standard_output + child.standard_error;
      if (child.exit_code == 0)
        message = "expected panic, but the test completed successfully";
      else if (!item.test.expected_panic->empty() &&
               combined.find(*item.test.expected_panic) == std::string::npos)
        message =
            "panic message did not contain '" + *item.test.expected_panic + "'";
      else
        status = TestStatus::Passed;
    } else if (child.exit_code == 0) {
      status = TestStatus::Passed;
    } else if (child.signaled) {
      if (child.standard_error.find("panic") != std::string::npos) {
        status = TestStatus::Failed;
        message = "test panicked unexpectedly";
      } else {
        status = TestStatus::Crashed;
        message = "test process terminated by a native signal";
      }
    } else {
      message = "test exited with status " + std::to_string(child.exit_code);
    }
    item.result = TestResult{item.test.identifier,
                             "unit",
                             item.test.source,
                             item.test.location,
                             status,
                             duration,
                             std::move(message),
                             child.standard_output,
                             child.standard_error};
  };

  if (options.test_fail_fast || options.test_jobs == 1) {
    for (Prepared &item : prepared) {
      execute(item);
      if (options.test_fail_fast && item.result->status != TestStatus::Passed &&
          item.result->status != TestStatus::Ignored)
        break;
    }
  } else {
    std::vector<std::size_t> parallel;
    std::vector<std::size_t> serial;
    for (std::size_t index = 0; index < prepared.size(); ++index)
      (prepared[index].test.serial ? serial : parallel).push_back(index);
    std::size_t next{};
    std::mutex mutex;
    const auto worker = [&] {
      while (true) {
        std::size_t index{};
        {
          std::lock_guard lock{mutex};
          if (next == parallel.size())
            return;
          index = parallel[next++];
        }
        execute(prepared[index]);
      }
    };
    std::vector<std::thread> workers;
    const std::size_t worker_count =
        std::min(options.test_jobs, std::max<std::size_t>(1, parallel.size()));
    for (std::size_t index = 0; index < worker_count; ++index)
      workers.emplace_back(worker);
    for (std::thread &thread : workers)
      thread.join();
    for (const std::size_t index : serial)
      execute(prepared[index]);
  }

  std::vector<TestResult> results;
  for (Prepared &item : prepared)
    if (item.result.has_value())
      results.push_back(std::move(*item.result));

  janus::driver::TemporaryDirectory doctest_directory =
      janus::driver::TemporaryDirectory::create("janus-doctest");
  for (std::size_t index = 0; index < doctests.size(); ++index) {
    const janus::driver::Doctest &test = doctests[index];
    const std::filesystem::path source =
        doctest_directory.path() /
        ("doctest-" + std::to_string(index) + ".janus");
    std::ofstream output{source, std::ios::binary};
    if (!output)
      throw std::runtime_error{"cannot create doctest source"};
    output << test.source;
    output.close();
    TestResult result{test.display_name(),
                      "doctest",
                      test.document,
                      {test.line, 1},
                      TestStatus::Error,
                      std::chrono::milliseconds{},
                      {},
                      {},
                      {}};
    const auto start = std::chrono::steady_clock::now();
    try {
      llvm::LLVMContext context;
      static_cast<void>(
          compile(source, context, toolchain, options.dependency_paths, false,
                  options.diagnostic_format, options.panic_trace));
      if (test.expectation == janus::driver::DoctestExpectation::CompilePass)
        result.status = TestStatus::Passed;
      else {
        result.status = TestStatus::Failed;
        result.message = "expected diagnostic " + test.expected_diagnostic;
      }
    } catch (const janus::CompileError &error) {
      const bool expected =
          test.expectation == janus::driver::DoctestExpectation::CompileFail &&
          std::any_of(error.diagnostics().begin(), error.diagnostics().end(),
                      [&test](const janus::Diagnostic &diagnostic) {
                        return janus::diagnostic_code_name(diagnostic.code) ==
                               test.expected_diagnostic;
                      });
      result.status = expected ? TestStatus::Passed : TestStatus::Error;
      if (!expected) {
        result.message = test.document.generic_string() + ':' +
                         std::to_string(test.line) + ": error: ";
        if (test.expectation == janus::driver::DoctestExpectation::CompileFail)
          result.message +=
              "expected diagnostic " + test.expected_diagnostic + ", got " +
              std::string{janus::diagnostic_code_name(error.diagnostic().code)};
        else
          result.message += "doctest compilation failed with " +
                            std::string{janus::diagnostic_code_name(
                                error.diagnostic().code)} +
                            ": " + error.what();
      }
    } catch (const std::exception &error) {
      result.status = TestStatus::Error;
      result.message = error.what();
    }
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    results.push_back(std::move(result));
    if (options.test_fail_fast && results.back().status != TestStatus::Passed)
      break;
  }
  print_test_report(results, options);
  return std::any_of(results.begin(), results.end(),
                     [](const TestResult &result) {
                       return result.status != TestStatus::Passed &&
                              result.status != TestStatus::Ignored;
                     })
             ? 1
             : 0;
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
      if (!options.doc_search.empty()) {
        const Toolchain toolchain = locate_toolchain(argv[0]);
        std::vector<janus::driver::ApiIndex> indexes;
        indexes.push_back(load_index_or_sources(
            toolchain.stdlib_api_index, {toolchain.stdlib / "std"},
            {"stdlib", JANUS_VERSION}));
        if (!options.doc_stdlib && options.manifest) {
          const std::filesystem::path package_index = first_api_index(
              {options.manifest->root() / "target/doc/api-index.json",
               options.manifest->root() / "api-index.json"});
          indexes.push_back(load_index_or_sources(
              package_index, {options.manifest->root() / "src"},
              {options.manifest->name, options.manifest->version}));
          const auto dependencies = janus::driver::resolve_dependencies(
              *options.manifest, {options.locked, true, false});
          for (const auto &dependency : dependencies) {
            const std::filesystem::path dependency_root = dependency.parent_path();
            const auto dependency_manifest = janus::driver::load_manifest(
                dependency_root / "janus.toml");
            const std::filesystem::path dependency_index = first_api_index(
                {dependency_root / "target/doc/api-index.json",
                 dependency_root / "api-index.json",
                 dependency_root / "docs/api-index.json"});
            indexes.push_back(load_index_or_sources(
                dependency_index, {dependency},
                {dependency_manifest.name, dependency_manifest.version}));
          }
        }
        const auto merged = janus::driver::merge_api_indexes(indexes);
        const auto results = janus::driver::search_api(
            merged, {options.doc_search, options.doc_module, options.doc_kind,
                     options.doc_package});
        std::cout << janus::driver::format_api_search(results,
                                                     options.doc_format);
        return results.empty() ? 1 : 0;
      }
      const std::filesystem::path output =
          options.output.empty()
              ? options.doc_stdlib ? std::filesystem::current_path() /
                                         "target" / "doc" / "stdlib"
                                   : options.manifest->root() / "target" / "doc"
              : std::filesystem::absolute(options.output).lexically_normal();
      const janus::driver::DocumentationReport report =
          options.doc_stdlib
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
          std::cerr << "error: undocumented standard-library module " << module
                    << '\n';
        for (const std::string &symbol : report.undocumented_symbols)
          std::cerr << "error: undocumented standard-library symbol " << symbol
                    << '\n';
        if (!report.unresolved_links.empty() ||
            !report.undocumented_modules.empty() ||
            !report.undocumented_symbols.empty() || !report.diagnostics.empty())
          return 1;
      }
      std::cout << "generated " << report.symbol_count << " public symbols in "
                << report.index_path.string() << '\n';
      if (options.doc_open)
        janus::driver::open_documentation(report.index_path);
      return 0;
    }
    const Toolchain toolchain = locate_toolchain(argv[0]);
    if (options.manifest.has_value()) {
      options.dependency_paths = janus::driver::resolve_dependencies(
          *options.manifest, {options.locked, options.offline});
      options.dependency_paths.push_back(options.manifest->root() / "src");
    }
    if (options.command == "test") {
      return run_tests(options, toolchain);
    }
    diagnostic_path = options.source;
    if (options.command == "check") {
      if (options.check_all_modules || options.deny_warnings) {
        const int status =
            check_sources(options, toolchain,
                          options.check_all_modules || options.deny_warnings);
        if (status == 0)
          std::cout << "checked " << options.source.string() << '\n';
        return status;
      }
      llvm::LLVMContext context;
      static_cast<void>(
          compile(options.source, context, toolchain, options.dependency_paths,
                  options.warn_high_growth_loops, options.diagnostic_format,
                  options.panic_trace));
      std::cout << "checked " << options.source.string() << '\n';
      return 0;
    }
    if (options.command == "build") {
      if (options.deny_warnings) {
        const int status = check_sources(options, toolchain, true);
        if (status != 0)
          return status;
      }
      CompilationTimings timings;
      const int status = build(
          options, default_output(options), toolchain,
          options.timings_format == Options::TimingsFormat::None ? nullptr
                                                                 : &timings);
      if (status == 0 && options.timings_format != Options::TimingsFormat::None)
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
