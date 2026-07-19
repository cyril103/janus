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
             << "entry = \"src/main.janus\"\n"
             << "\n[dependencies]\n"
             << "local = { path = \"../local\", version = \"^1.0.0\" }\n"
             << "remote = { git = \"https://example.invalid/repo\", "
                "rev = \"0123456789abcdef0123456789abcdef01234567\" }\n";
    }
    const janus::driver::Manifest manifest = janus::driver::load_manifest(path);
    require(manifest.name == "hello-world", "package name was not parsed");
    require(manifest.version == "1.2.3-beta.1",
            "package version was not parsed");
    require(manifest.entry == "src/main.janus", "entry was not parsed");
    require(manifest.entry_path() == directory / "src/main.janus",
            "entry path was not resolved from the project root");
    require(janus::driver::find_manifest(directory / "src") == path,
            "manifest was not found from a child directory");
    require(manifest.dependencies.size() == 2, "dependencies were not parsed");
    require(manifest.dependencies[0].path == "../local",
            "path dependency was not retained");
    require(manifest.dependencies[0].version_requirement == "^1.0.0",
            "version requirement was not retained");
    require(manifest.dependencies[1].is_git() &&
                manifest.dependencies[1].revision.size() == 40,
            "Git dependency was not retained");

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
