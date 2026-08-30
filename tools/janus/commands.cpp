#include "commands.hpp"

#include "janus/diagnostics/catalog.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace janus::cli {

void print_usage(std::ostream &output) {
  output << "usage:\n"
         << "  janus new <directory> [--name <name>]\n"
         << "  janus init [directory] [--name <name>]\n"
         << "  janus add <name>[@<version>] [--path <path> | "
            "--git <url> --rev <commit>] [--registry <url>]\n"
         << "  janus remove <name>\n"
         << "  janus search <query> [--registry <url>]\n"
         << "  janus publish [--registry <url>]\n"
         << "  janus explain <diagnostic-code>\n"
         << "  janus clean\n"
         << "  janus check [source.janus] [--all] [--deny-warnings] "
            "[--diagnostic-format human|json]\n"
         << "  janus build [source.janus] [-o output] [--release] "
            "[--emit llvm-ir|object] [--panic-trace full|short|off] "
            "[--diagnostic-format human|json] [--timings[=human|json]] "
            "[--no-cache] [--deny-warnings]\n"
         << "  janus run [source.janus] [--release] "
            "[--panic-trace full|short|off] [-- [arguments...]]\n"
         << "  janus test [filter] [--doc] [--doc-path <path>] "
            "[--list] [--exact] [--ignored|--include-ignored] "
            "[--jobs <count>] [--timeout <duration>] [--fail-fast] "
            "[--fail-if-empty] [--format human|json|junit] [--release] "
            "[--panic-trace full|short|off]\n"
         << "  janus fmt [source.janus] [--check]\n"
         << "  janus doc [--stdlib] [-o directory] [--open] [--offline] "
            "[--search QUERY] [--format human|json] "
            "[--module NAME] [--kind KIND] [--package NAME]\n"
         << "  diagnostics: --warn-high-growth-loops for check, build, run\n"
         << "  dependency options: --locked --offline\n"
         << "  janus --help\n"
         << "  janus --version\n";
}

int explain_diagnostic(int argc, char **argv) {
  if (argc != 3)
    throw std::runtime_error{"explain requires one diagnostic code"};
  const auto code = diagnostics::diagnostic_code_from_name(argv[2]);
  if (!code)
    throw std::runtime_error{"unknown diagnostic code '" +
                             std::string{argv[2]} + "'"};
  const diagnostics::DiagnosticExplanation explanation =
      diagnostics::explain_diagnostic(*code);
  std::cout << diagnostic_code_name(explanation.code) << ": "
            << explanation.title << "\n\n"
            << explanation.explanation << "\n\nhelp: " << explanation.action
            << '\n';
  return 0;
}

void print_command_usage(std::ostream &output, std::string_view command) {
  output << "usage: janus " << command;
  if (command == "check")
    output << " [source.janus] [--locked] [--offline] [--all] "
              "[--deny-warnings] [--warn-high-growth-loops] "
              "[--diagnostic-format human|json]\n";
  else if (command == "build")
    output << " [source.janus] [-o output] [--release] "
              "[--emit llvm-ir|object] [--locked] [--offline] "
              "[--panic-trace full|short|off] [--warn-high-growth-loops] "
              "[--diagnostic-format human|json] [--timings[=human|json]] "
              "[--no-cache] [--deny-warnings]\n";
  else if (command == "run")
    output << " [source.janus] [--release] [--locked] [--offline] "
              "[--panic-trace full|short|off] [--warn-high-growth-loops] "
              "[-- [arguments...]]\n";
  else if (command == "test")
    output << " [filter] [--doc] [--doc-path <path>] [--list] [--exact] "
              "[--ignored|--include-ignored] [--jobs <count>] "
              "[--timeout <duration>] [--fail-fast] [--fail-if-empty] "
              "[--format human|json|junit] [--release] [--locked] "
              "[--offline] [--panic-trace full|short|off]\n";
  else if (command == "doc")
    output << " [--stdlib] [-o directory] [--open] [--offline] "
              "[--search QUERY] [--format human|json] [--module NAME] "
              "[--kind KIND] [--package NAME]\n";
  else if (command == "clean")
    output << '\n';
  else
    output << " [source.janus] [--check]\n";
}

bool is_execution_command(std::string_view command) {
  return command == "check" || command == "build" || command == "run" ||
         command == "test" || command == "doc" || command == "clean";
}

} // namespace janus::cli
