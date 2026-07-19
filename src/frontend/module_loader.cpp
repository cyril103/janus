#include "janus/frontend/module_loader.hpp"

#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace janus::frontend {

ModuleLoader::ModuleLoader(std::vector<std::filesystem::path> search_paths)
    : search_paths_{std::move(search_paths)} {}

ast::Program ModuleLoader::load(const std::filesystem::path &entry_path) {
  loaded_paths_.clear();
  const std::filesystem::path absolute =
      std::filesystem::absolute(entry_path).lexically_normal();
  return load_file(absolute, absolute.parent_path(), nullptr);
}

ast::Program ModuleLoader::load(const std::filesystem::path &entry_path,
                                std::string_view entry_source) {
  loaded_paths_.clear();
  const std::filesystem::path absolute =
      std::filesystem::absolute(entry_path).lexically_normal();
  return load_file(absolute, absolute.parent_path(), nullptr, &entry_source);
}

ast::Program ModuleLoader::load_file(const std::filesystem::path &path,
                                     const std::filesystem::path &project_root,
                                     const std::string *expected_module,
                                     const std::string_view *source_override) {
  const std::filesystem::path normalized =
      std::filesystem::absolute(path).lexically_normal();
  if (std::find(loaded_paths_.begin(), loaded_paths_.end(), normalized) !=
      loaded_paths_.end())
    return {};
  loaded_paths_.push_back(normalized);

  std::string source;
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
  Parser parser{source};
  ast::Program parsed = parser.parse_program();

  if (expected_module != nullptr && (!parsed.module_name.has_value() ||
                                     *parsed.module_name != *expected_module))
    throw CompileError{SourceLocation{}, "module file '" + normalized.string() +
                                             "' must declare 'module " +
                                             *expected_module + "'"};

  ast::Program result;
  for (const std::string &import : parsed.imports) {
    ast::Program dependency =
        load_file(resolve_import(import, project_root), project_root, &import);
    for (ast::TraitDeclaration &trait_declaration : dependency.traits)
      result.traits.push_back(std::move(trait_declaration));
    for (ast::EnumDeclaration &enum_declaration : dependency.enums)
      result.enums.push_back(std::move(enum_declaration));
    for (ast::ClassDeclaration &class_declaration : dependency.classes)
      result.classes.push_back(std::move(class_declaration));
    for (ast::FunctionDeclaration &function : dependency.functions)
      result.functions.push_back(std::move(function));
  }
  for (ast::TraitDeclaration &trait_declaration : parsed.traits)
    result.traits.push_back(std::move(trait_declaration));
  for (ast::EnumDeclaration &enum_declaration : parsed.enums)
    result.enums.push_back(std::move(enum_declaration));
  for (ast::ClassDeclaration &class_declaration : parsed.classes)
    result.classes.push_back(std::move(class_declaration));
  for (ast::FunctionDeclaration &function : parsed.functions)
    result.functions.push_back(std::move(function));
  result.module_name = std::move(parsed.module_name);
  return result;
}

std::filesystem::path
ModuleLoader::resolve_import(std::string_view module,
                             const std::filesystem::path &project_root) const {
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
  roots.insert(roots.end(), search_paths_.begin(), search_paths_.end());
  for (const std::filesystem::path &root : roots) {
    const std::filesystem::path candidate = root / relative;
    if (std::filesystem::is_regular_file(candidate))
      return candidate;
  }
  throw std::runtime_error{"cannot resolve imported module '" +
                           std::string{module} + "'"};
}

} // namespace janus::frontend
