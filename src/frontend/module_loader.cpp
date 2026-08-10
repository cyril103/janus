#include "janus/frontend/module_loader.hpp"
#include "module_resolution.hpp"

#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace janus::frontend {

namespace {

std::filesystem::path normalize_path(const std::filesystem::path &path) {
  return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

class VisitingPathGuard final {
public:
  VisitingPathGuard(std::unordered_set<std::filesystem::path> &paths,
                    std::filesystem::path path)
      : paths_{paths}, path_{std::move(path)} {}

  ~VisitingPathGuard() { paths_.erase(path_); }

  VisitingPathGuard(const VisitingPathGuard &) = delete;
  VisitingPathGuard &operator=(const VisitingPathGuard &) = delete;

private:
  std::unordered_set<std::filesystem::path> &paths_;
  std::filesystem::path path_;
};

CompileError with_source_path(const CompileError &error,
                              const std::filesystem::path &path) {
  std::vector<Diagnostic> diagnostics = error.diagnostics();
  for (Diagnostic &diagnostic : diagnostics)
    if (diagnostic.source_path.empty())
      diagnostic.source_path = path;
  return CompileError{std::move(diagnostics)};
}

void require_module_name(const ast::Program &program,
                         const std::string *expected_module,
                         const std::filesystem::path &path) {
  if (expected_module == nullptr || (program.module_name.has_value() &&
                                     *program.module_name == *expected_module))
    return;
  throw CompileError{Diagnostic{
      DiagnosticSeverity::Error,
      DiagnosticCode::Unclassified,
      "module file '" + path.string() + "' must declare 'module " +
          *expected_module + "'",
      SourceLocation{},
      {},
      {},
      {},
      path,
  }};
}

template <typename Declaration>
bool declares_public_symbol(const std::vector<Declaration> &declarations,
                            std::string_view module, std::string_view name) {
  return std::any_of(declarations.begin(), declarations.end(),
                     [&](const Declaration &declaration) {
                       return declaration.module_name == module &&
                              declaration.name == name &&
                              !declaration.is_private;
                     });
}

bool declares_public_symbol(const ast::Program &program,
                            std::string_view module, std::string_view name) {
  if (declares_public_symbol(program.functions, module, name) ||
      declares_public_symbol(program.classes, module, name) ||
      declares_public_symbol(program.traits, module, name) ||
      declares_public_symbol(program.enums, module, name))
    return true;
  return std::any_of(program.globals.begin(), program.globals.end(),
                     [&](const ast::GlobalDeclaration &global) {
                       return global.module_name == module &&
                              global.declaration.name == name &&
                              !global.declaration.is_private;
                     });
}

bool declares_private_symbol(const ast::Program &program,
                             std::string_view module, std::string_view name) {
  const auto private_declaration = [&](const auto &declarations) {
    return std::any_of(
        declarations.begin(), declarations.end(), [&](const auto &declaration) {
          return declaration.module_name == module &&
                 declaration.name == name && declaration.is_private;
        });
  };
  if (private_declaration(program.functions) ||
      private_declaration(program.classes) ||
      private_declaration(program.traits) || private_declaration(program.enums))
    return true;
  return std::any_of(program.globals.begin(), program.globals.end(),
                     [&](const ast::GlobalDeclaration &global) {
                       return global.module_name == module &&
                              global.declaration.name == name &&
                              global.declaration.is_private;
                     });
}

void reserve_import_name(std::unordered_map<std::string, std::string> &names,
                         std::string local_name, std::string origin,
                         SourceLocation location,
                         const std::filesystem::path &source_path) {
  const auto [existing, inserted] =
      names.emplace(std::move(local_name), std::move(origin));
  if (!inserted && existing->second != origin)
    throw CompileError{Diagnostic{
        DiagnosticSeverity::Error,
        DiagnosticCode::Unclassified,
        "import name '" + existing->first + "' is ambiguous between '" +
            existing->second + "' and '" + origin +
            "'; qualify or rename one import",
        location,
        {},
        {},
        {},
        source_path,
    }};
}

} // namespace

ModuleLoader::ModuleLoader(std::vector<std::filesystem::path> search_paths)
    : search_paths_{std::move(search_paths)} {}

ast::Program ModuleLoader::load(const std::filesystem::path &entry_path,
                                ModuleLoadTimings *timings) {
  visiting_paths_.clear();
  loaded_programs_.clear();
  load_order_.clear();
  const std::filesystem::path absolute = normalize_path(entry_path);
  static_cast<void>(
      load_file(absolute, absolute.parent_path(), nullptr, nullptr, timings));
  return take_loaded_program(absolute);
}

ast::Program ModuleLoader::load(const std::filesystem::path &entry_path,
                                std::string_view entry_source,
                                ModuleLoadTimings *timings) {
  visiting_paths_.clear();
  loaded_programs_.clear();
  load_order_.clear();
  const std::filesystem::path absolute = normalize_path(entry_path);
  static_cast<void>(load_file(absolute, absolute.parent_path(), nullptr,
                              &entry_source, timings));
  return take_loaded_program(absolute);
}

const ast::Program &
ModuleLoader::load_file(const std::filesystem::path &path,
                        const std::filesystem::path &project_root,
                        const std::string *expected_module,
                        const std::string_view *source_override,
                        ModuleLoadTimings *timings) {
  const std::filesystem::path normalized = normalize_path(path);
  if (const auto loaded = loaded_programs_.find(normalized);
      loaded != loaded_programs_.end()) {
    require_module_name(*loaded->second, expected_module, normalized);
    return *loaded->second;
  }
  if (!visiting_paths_.insert(normalized).second)
    throw CompileError{SourceLocation{}, "cyclic module import involving '" +
                                             normalized.string() + "'"};
  const VisitingPathGuard visiting_guard{visiting_paths_, normalized};

  std::string source;
  const auto loading_start = timings == nullptr
                                 ? std::chrono::steady_clock::time_point{}
                                 : std::chrono::steady_clock::now();
  if (source_override != nullptr) {
    source = *source_override;
  } else {
    std::ifstream input{normalized, std::ios::binary};
    if (!input)
      throw std::runtime_error{"cannot open module source '" +
                               normalized.string() + "'"};
    source.assign(std::istreambuf_iterator<char>{input},
                  std::istreambuf_iterator<char>{});
  }
  if (timings != nullptr)
    timings->loading += std::chrono::steady_clock::now() - loading_start;
  const auto parsing_start = timings == nullptr
                                 ? std::chrono::steady_clock::time_point{}
                                 : std::chrono::steady_clock::now();
  ast::Program parsed;
  try {
    Parser parser{source};
    parsed = parser.parse_program();
  } catch (const CompileError &error) {
    if (expected_module != nullptr)
      throw with_source_path(error, normalized);
    throw;
  }
  if (timings != nullptr)
    timings->parsing += std::chrono::steady_clock::now() - parsing_start;

  require_module_name(parsed, expected_module, normalized);

  std::unordered_map<std::string, std::string> imported_names;
  const auto reserve_local = [&](std::string_view name,
                                 SourceLocation location) {
    reserve_import_name(imported_names, std::string{name}, "local declaration",
                        location, normalized);
  };
  for (const ast::FunctionDeclaration &declaration : parsed.functions)
    reserve_local(declaration.name, declaration.location);
  for (const ast::ClassDeclaration &declaration : parsed.classes)
    reserve_local(declaration.name, declaration.location);
  for (const ast::TraitDeclaration &declaration : parsed.traits)
    reserve_local(declaration.name, declaration.location);
  for (const ast::EnumDeclaration &declaration : parsed.enums)
    reserve_local(declaration.name, declaration.location);
  for (const ast::GlobalDeclaration &global : parsed.globals)
    reserve_local(global.declaration.name, global.declaration.location);
  for (const ast::ImportDeclaration &import : parsed.imports) {
    const ast::Program *dependency = nullptr;
    try {
      dependency =
          &load_file(resolve_import(import.module_name, project_root),
                     project_root, &import.module_name, nullptr, timings);
    } catch (const CompileError &error) {
      throw with_source_path(error, normalized);
    }
    if (import.module_alias.has_value())
      reserve_import_name(imported_names, *import.module_alias,
                          import.module_name, import.location, normalized);
    for (const ast::ImportDeclaration::Symbol &symbol : import.symbols) {
      if (!declares_public_symbol(*dependency, import.module_name,
                                  symbol.name)) {
        const std::string reason =
            declares_private_symbol(*dependency, import.module_name,
                                    symbol.name)
                ? " is private"
                : " does not exist";
        throw CompileError{Diagnostic{DiagnosticSeverity::Error,
                                      DiagnosticCode::Unclassified,
                                      "symbol '" + import.module_name + "." +
                                          symbol.name + "'" + reason,
                                      symbol.location,
                                      {},
                                      {},
                                      {},
                                      normalized}};
      }
      reserve_import_name(imported_names, symbol.alias.value_or(symbol.name),
                          import.module_name + "." + symbol.name,
                          symbol.location, normalized);
    }
  }
  load_order_.push_back(normalized);
  const auto [loaded, inserted] = loaded_programs_.emplace(
      normalized, std::make_unique<ast::Program>(std::move(parsed)));
  static_cast<void>(inserted);
  return *loaded->second;
}

ast::Program
ModuleLoader::take_loaded_program(const std::filesystem::path &entry_path) {
  ast::Program result;
  for (const std::filesystem::path &path : load_order_) {
    ast::Program &program = *loaded_programs_.at(path);
    result.globals.insert(result.globals.end(),
                          std::make_move_iterator(program.globals.begin()),
                          std::make_move_iterator(program.globals.end()));
    result.traits.insert(result.traits.end(),
                         std::make_move_iterator(program.traits.begin()),
                         std::make_move_iterator(program.traits.end()));
    result.enums.insert(result.enums.end(),
                        std::make_move_iterator(program.enums.begin()),
                        std::make_move_iterator(program.enums.end()));
    result.classes.insert(result.classes.end(),
                          std::make_move_iterator(program.classes.begin()),
                          std::make_move_iterator(program.classes.end()));
    result.functions.insert(result.functions.end(),
                            std::make_move_iterator(program.functions.begin()),
                            std::make_move_iterator(program.functions.end()));
    result.imports.insert(result.imports.end(),
                          std::make_move_iterator(program.imports.begin()),
                          std::make_move_iterator(program.imports.end()));
    if (path == entry_path) {
      result.module_name = std::move(program.module_name);
      result.documentation = std::move(program.documentation);
    }
  }
  return result;
}

std::filesystem::path detail::resolve_module_import(
    std::string_view module, const std::filesystem::path &project_root,
    const std::vector<std::filesystem::path> &search_paths) {
  std::filesystem::path relative;
  std::size_t start = 0;
  while (start < module.size()) {
    const std::size_t separator = module.find('.', start);
    relative /= module.substr(start, separator == std::string_view::npos
                                         ? module.size() - start
                                         : separator - start);
    if (separator == std::string_view::npos)
      break;
    start = separator + 1;
  }
  relative += ".janus";

  std::vector<std::filesystem::path> roots{project_root};
  roots.insert(roots.end(), search_paths.begin(), search_paths.end());
  for (const std::filesystem::path &root : roots) {
    const std::filesystem::path candidate = root / relative;
    if (std::filesystem::is_regular_file(candidate))
      return normalize_path(candidate);
  }
  throw CompileError{DiagnosticCode::ModuleNotFound, SourceLocation{},
                     "cannot resolve imported module '" + std::string{module} +
                         "'"};
}

std::filesystem::path
ModuleLoader::resolve_import(std::string_view module,
                             const std::filesystem::path &project_root) const {
  return detail::resolve_module_import(module, project_root, search_paths_);
}

} // namespace janus::frontend
