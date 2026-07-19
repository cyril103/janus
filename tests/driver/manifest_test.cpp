#include "janus/driver/manifest.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error{message};
}

} // namespace

int main() {
  try {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "janus-manifest-test";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "janus.toml";
    {
      std::ofstream output{path};
      output << "# A Janus package\n"
             << "[package]\n"
             << "name = \"hello-world\"\n"
             << "version = \"1.2.3-beta.1\"\n"
             << "entry = \"src/main.janus\"\n";
    }
    const janus::driver::Manifest manifest = janus::driver::load_manifest(path);
    require(manifest.name == "hello-world", "package name was not parsed");
    require(manifest.version == "1.2.3-beta.1",
            "package version was not parsed");
    require(manifest.entry == "src/main.janus", "entry was not parsed");
    require(manifest.entry_path() == directory / "src/main.janus",
            "entry path was not resolved from the project root");

    {
      std::ofstream output{path};
      output << "[package]\n"
             << "name = \"bad name\"\n"
             << "version = \"1\"\n"
             << "entry = \"/main.janus\"\n";
    }
    bool rejected = false;
    try {
      static_cast<void>(janus::driver::load_manifest(path));
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "invalid package metadata was accepted");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
