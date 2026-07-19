#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::filesystem::path home() {
  if (const char *configured = std::getenv("JANUSUP_HOME"))
    return configured;
#ifdef _WIN32
  if (const char *local = std::getenv("LOCALAPPDATA"))
    return std::filesystem::path{local} / "Janus";
#else
  if (const char *user = std::getenv("HOME"))
    return std::filesystem::path{user} / ".janus";
#endif
  throw std::runtime_error{"cannot determine Janus home directory"};
}

std::filesystem::path toolchains() { return home() / "toolchains"; }
std::filesystem::path default_file() { return home() / "default"; }

std::string active_toolchain() {
  std::ifstream input{default_file()};
  std::string name;
  std::getline(input, name);
  return name;
}

void validate_name(const std::string &name) {
  if (name.empty() || name == "." || name == ".." ||
      name.find_first_of("/\\") != std::string::npos)
    throw std::runtime_error{"invalid toolchain name '" + name + "'"};
}

void activate(const std::string &name) {
  validate_name(name);
  const std::filesystem::path source = toolchains() / name / "bin";
  if (!std::filesystem::is_directory(source))
    throw std::runtime_error{"toolchain '" + name + "' is not installed"};
  std::filesystem::create_directories(home() / "bin");
  for (const char *program : {"janus", "janusc", "janusup"}) {
#ifdef _WIN32
    const std::filesystem::path filename = std::string{program} + ".exe";
    if (std::filesystem::exists(source / filename))
      std::filesystem::copy_file(
          source / filename, home() / "bin" / filename,
          std::filesystem::copy_options::overwrite_existing);
#else
    const std::filesystem::path shim = home() / "bin" / program;
    std::error_code ignored;
    std::filesystem::remove(shim, ignored);
    if (std::filesystem::exists(source / program))
      std::filesystem::create_symlink(std::filesystem::path{"../toolchains"} /
                                          name / "bin" / program,
                                      shim);
#endif
  }
  std::ofstream output{default_file(), std::ios::trunc};
  output << name << '\n';
}

void install(const std::filesystem::path &source, const std::string &name) {
  validate_name(name);
  if (!std::filesystem::is_regular_file(source / "bin/janus")
#ifdef _WIN32
      && !std::filesystem::is_regular_file(source / "bin/janus.exe")
#endif
  )
    throw std::runtime_error{"the package does not contain bin/janus"};
  std::filesystem::create_directories(toolchains());
  const std::filesystem::path destination = toolchains() / name;
  if (std::filesystem::exists(destination))
    throw std::runtime_error{"toolchain '" + name + "' is already installed"};
  std::filesystem::copy(source, destination,
                        std::filesystem::copy_options::recursive);
  activate(name);
  std::cout << "installed and selected Janus toolchain '" << name << "'\n";
}

void list() {
  const std::string active = active_toolchain();
  if (!std::filesystem::exists(toolchains()))
    return;
  for (const auto &entry : std::filesystem::directory_iterator(toolchains())) {
    if (entry.is_directory())
      std::cout << (entry.path().filename() == active ? "* " : "  ")
                << entry.path().filename().string() << '\n';
  }
}

void usage() {
  std::cerr << "usage:\n"
            << "  janusup install <package-directory> [name]\n"
            << "  janusup default <name>\n"
            << "  janusup list\n"
            << "  janusup home\n"
            << "  janusup --version\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
      std::cout << "janusup " << JANUS_VERSION << '\n';
      return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "home") {
      std::cout << home().string() << '\n';
      return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "list") {
      list();
      return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "default") {
      activate(argv[2]);
      std::cout << "selected Janus toolchain '" << argv[2] << "'\n";
      return 0;
    }
    if ((argc == 3 || argc == 4) && std::string_view{argv[1]} == "install") {
      install(argv[2], argc == 4 ? argv[3] : "stable");
      return 0;
    }
    usage();
  } catch (const std::exception &error) {
    std::cerr << "janusup: error: " << error.what() << '\n';
  }
  return 1;
}
