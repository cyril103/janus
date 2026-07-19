#include "janus/driver/registry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string trim(std::string value) {
  const auto whitespace = [](unsigned char character) {
    return std::isspace(character) != 0;
  };
  value.erase(value.begin(),
              std::find_if_not(value.begin(), value.end(), whitespace));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(),
              value.end());
  return value;
}

void validate_text(std::string_view value, std::string_view description) {
  if (value.empty() || value.find_first_of("\"\r\n") != std::string_view::npos)
    throw std::runtime_error{"invalid " + std::string{description}};
}

std::string dependency_line(const janus::driver::Dependency &dependency) {
  validate_text(dependency.name, "dependency name");
  validate_text(dependency.version_requirement, "version requirement");
  if (dependency.is_registry())
    return dependency.name + " = \"" + dependency.version_requirement + "\"";
  std::string result = dependency.name + " = { ";
  if (dependency.is_git()) {
    validate_text(dependency.git, "Git URL");
    validate_text(dependency.revision, "Git revision");
    result += "git = \"" + dependency.git + "\", rev = \"" +
              dependency.revision + "\"";
  } else {
    validate_text(dependency.path.generic_string(), "dependency path");
    result += "path = \"" + dependency.path.generic_string() + "\"";
  }
  if (!dependency.version_requirement.empty())
    result += ", version = \"" + dependency.version_requirement + "\"";
  return result + " }";
}

} // namespace

namespace janus::driver {

std::filesystem::path registry_root() {
  if (const char *configured = std::getenv("JANUS_REGISTRY"))
    return configured;
  if (const char *janus_home = std::getenv("JANUSUP_HOME"))
    return std::filesystem::path{janus_home} / "registry";
#ifdef _WIN32
  if (const char *local = std::getenv("LOCALAPPDATA"))
    return std::filesystem::path{local} / "Janus/registry";
#else
  if (const char *home = std::getenv("HOME"))
    return std::filesystem::path{home} / ".janus/registry";
#endif
  throw std::runtime_error{"cannot determine the Janus registry directory"};
}

void publish_package(const Manifest &manifest) {
  if (!std::filesystem::is_directory(manifest.root() / "src"))
    throw std::runtime_error{"cannot publish a package without src/"};
  for (const Dependency &dependency : manifest.dependencies)
    if (!dependency.is_git() && !dependency.is_registry())
      throw std::runtime_error{"cannot publish path dependency '" +
                               dependency.name + "'"};
  const std::filesystem::path destination =
      registry_root() / manifest.name / manifest.version / "package";
  if (std::filesystem::exists(destination))
    throw std::runtime_error{"package " + manifest.name + " " +
                             manifest.version + " is already published"};
  const std::filesystem::path staging = destination.string() + ".new";
  std::filesystem::remove_all(staging);
  std::filesystem::create_directories(staging);
  try {
    std::filesystem::copy_file(manifest.path, staging / "janus.toml");
    std::filesystem::copy(manifest.root() / "src", staging / "src",
                          std::filesystem::copy_options::recursive);
    for (const char *document : {"README.md", "LICENSE"}) {
      if (std::filesystem::is_regular_file(manifest.root() / document))
        std::filesystem::copy_file(manifest.root() / document,
                                   staging / document);
    }
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::rename(staging, destination);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(staging, ignored);
    throw;
  }
}

void add_dependency(const std::filesystem::path &manifest_path,
                    const Dependency &dependency) {
  Manifest manifest = load_manifest(manifest_path);
  for (const Dependency &existing : manifest.dependencies)
    if (existing.name == dependency.name)
      throw std::runtime_error{"dependency '" + dependency.name +
                               "' already exists"};

  std::ifstream input{manifest_path};
  std::string contents{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
  if (!contents.ends_with('\n'))
    contents += '\n';
  if (contents.find("[dependencies]") == std::string::npos)
    contents += "\n[dependencies]\n";
  contents += dependency_line(dependency) + '\n';
  const std::filesystem::path temporary = manifest_path.string() + ".new";
  {
    std::ofstream output{temporary};
    output << contents;
  }
  static_cast<void>(load_manifest(temporary));
  std::filesystem::remove(manifest_path);
  std::filesystem::rename(temporary, manifest_path);
}

void remove_dependency(const std::filesystem::path &manifest_path,
                       const std::string &name) {
  const Manifest manifest = load_manifest(manifest_path);
  if (std::none_of(manifest.dependencies.begin(), manifest.dependencies.end(),
                   [&name](const Dependency &dependency) {
                     return dependency.name == name;
                   }))
    throw std::runtime_error{"dependency '" + name + "' does not exist"};
  std::ifstream input{manifest_path};
  std::vector<std::string> lines;
  std::string line;
  bool in_dependencies = false;
  while (std::getline(input, line)) {
    const std::string stripped = trim(line);
    if (stripped.starts_with('['))
      in_dependencies = stripped == "[dependencies]";
    bool remove = false;
    if (in_dependencies) {
      const std::size_t equals = stripped.find('=');
      remove = equals != std::string::npos &&
               trim(stripped.substr(0, equals)) == name;
    }
    if (!remove)
      lines.push_back(line);
  }
  const std::filesystem::path temporary = manifest_path.string() + ".new";
  {
    std::ofstream output{temporary};
    for (const std::string &kept : lines)
      output << kept << '\n';
  }
  static_cast<void>(load_manifest(temporary));
  std::filesystem::remove(manifest_path);
  std::filesystem::rename(temporary, manifest_path);
}

} // namespace janus::driver
