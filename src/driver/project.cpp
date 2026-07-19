#include "janus/driver/project.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>

namespace {

std::string inferred_name(const std::filesystem::path &directory) {
  std::string name = directory.filename().string();
  if (name.empty())
    name = "janus-project";
  for (char &character : name) {
    const auto value = static_cast<unsigned char>(character);
    if (!std::isalnum(value) && character != '_' && character != '-')
      character = '-';
  }
  if (!std::isalpha(static_cast<unsigned char>(name.front())))
    name = "janus-" + name;
  return name;
}

std::string project_name(const std::filesystem::path &directory,
                         const std::string &requested) {
  const std::string name =
      requested.empty() ? inferred_name(directory) : requested;
  if (!std::regex_match(name, std::regex{"[A-Za-z][A-Za-z0-9_-]*"}))
    throw std::runtime_error{"invalid project name '" + name + "'"};
  return name;
}

void write_if_missing(const std::filesystem::path &path,
                      const std::string &contents) {
  if (std::filesystem::exists(path))
    return;
  std::ofstream output{path};
  if (!output)
    throw std::runtime_error{"cannot create '" + path.string() + "'"};
  output << contents;
}

} // namespace

namespace janus::driver {

void initialize_project(const std::filesystem::path &directory,
                        const std::string &name) {
  const std::filesystem::path root =
      std::filesystem::absolute(directory).lexically_normal();
  std::filesystem::create_directories(root);
  if (std::filesystem::exists(root / "janus.toml"))
    throw std::runtime_error{"a Janus project already exists in '" +
                             root.string() + "'"};

  const std::string resolved_name = project_name(root, name);
  std::filesystem::create_directories(root / "src");
  std::filesystem::create_directories(root / "tests");
  write_if_missing(root / "src/main.janus",
                   "def main() : int {\n"
                   "    println(\"Hello from Janus!\")\n"
                   "    return 0\n"
                   "}\n");
  write_if_missing(root / ".gitignore", "/target/\n");

  std::ofstream manifest{root / "janus.toml"};
  if (!manifest)
    throw std::runtime_error{"cannot create janus.toml"};
  manifest << "[package]\n"
           << "name = \"" << resolved_name << "\"\n"
           << "version = \"0.1.0\"\n"
           << "entry = \"src/main.janus\"\n";
}

void create_project(const std::filesystem::path &directory,
                    const std::string &name) {
  const std::filesystem::path root =
      std::filesystem::absolute(directory).lexically_normal();
  if (std::filesystem::exists(root))
    throw std::runtime_error{"destination '" + root.string() +
                             "' already exists"};
  initialize_project(root, name);
}

} // namespace janus::driver
