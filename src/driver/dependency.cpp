#include "janus/driver/dependency.hpp"
#include "janus/driver/registry.hpp"
#include "janus/driver/semver.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

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

void run(const std::string &command, const std::string &description) {
  if (command_status(std::system(command.c_str())) != 0)
    throw std::runtime_error{description};
}

std::filesystem::path cache_root() {
  if (const char *configured = std::getenv("JANUS_CACHE"))
    return configured;
  if (const char *janus_home = std::getenv("JANUSUP_HOME"))
    return std::filesystem::path{janus_home} / "cache";
#ifdef _WIN32
  if (const char *local = std::getenv("LOCALAPPDATA"))
    return std::filesystem::path{local} / "Janus/cache";
#else
  if (const char *home = std::getenv("HOME"))
    return std::filesystem::path{home} / ".janus/cache";
#endif
  throw std::runtime_error{"cannot determine the Janus cache directory"};
}

std::filesystem::path resolve_git(const janus::driver::Dependency &dependency,
                                  bool offline) {
  const std::filesystem::path cache =
      cache_root() / "git" / dependency.revision;
  const std::filesystem::path revision_file = cache / ".janus-revision";
  if (std::filesystem::exists(cache)) {
    std::ifstream input{revision_file};
    std::string cached_revision;
    input >> cached_revision;
    if (cached_revision != dependency.revision)
      throw std::runtime_error{"cached dependency '" + dependency.name +
                               "' has an unexpected revision"};
  } else {
    if (offline)
      throw std::runtime_error{"dependency '" + dependency.name +
                               "' is not cached and --offline was requested"};
    const std::filesystem::path staging = cache.string() + ".new";
    std::filesystem::create_directories(staging.parent_path());
    std::filesystem::remove_all(staging);
    try {
      run("git clone --quiet --no-checkout " + shell_quote(dependency.git) +
              " " + shell_quote(staging),
          "could not clone dependency '" + dependency.name + "'");
      run("git -C " + shell_quote(staging) + " checkout --quiet --detach " +
              shell_quote(dependency.revision),
          "could not check out dependency '" + dependency.name + "' at " +
              dependency.revision);
      std::ofstream revision{staging / ".janus-revision"};
      revision << dependency.revision << '\n';
      revision.close();
      std::filesystem::rename(staging, cache);
    } catch (...) {
      std::error_code ignored;
      std::filesystem::remove_all(staging, ignored);
      throw;
    }
  }
  return cache;
}

std::map<std::string, std::string>
locked_versions(const std::filesystem::path &path) {
  std::ifstream input{path};
  std::map<std::string, std::string> result;
  std::string name;
  std::string line;
  while (std::getline(input, line)) {
    const auto quoted_value = [&line]() {
      const std::size_t first = line.find('"');
      const std::size_t last = line.rfind('"');
      return first != std::string::npos && last > first
                 ? line.substr(first + 1, last - first - 1)
                 : std::string{};
    };
    if (line.starts_with("name = "))
      name = quoted_value();
    else if (!name.empty() && line.starts_with("version = ")) {
      result[name] = quoted_value();
      name.clear();
    }
  }
  return result;
}

std::filesystem::path
resolve_registry(const janus::driver::Dependency &dependency, bool offline,
                 const std::string &locked_version) {
  std::string selected = locked_version;
  const std::filesystem::path package_versions =
      janus::driver::registry_root() / dependency.name;
  if (selected.empty()) {
    if (!std::filesystem::is_directory(package_versions))
      throw std::runtime_error{"package '" + dependency.name +
                               "' does not exist in the Janus registry"};
    bool found = false;
    janus::driver::SemanticVersion best;
    for (const auto &entry :
         std::filesystem::directory_iterator(package_versions)) {
      if (!entry.is_directory())
        continue;
      try {
        const janus::driver::SemanticVersion candidate =
            janus::driver::parse_semantic_version(
                entry.path().filename().string());
        if (janus::driver::matches_version(dependency.version_requirement,
                                           candidate) &&
            (!found || janus::driver::compare(candidate, best) > 0)) {
          best = candidate;
          selected = candidate.str();
          found = true;
        }
      } catch (const std::runtime_error &) {
      }
    }
    if (!found)
      throw std::runtime_error{"no version of package '" + dependency.name +
                               "' satisfies '" +
                               dependency.version_requirement + "'"};
  }
  if (!janus::driver::matches_version(
          dependency.version_requirement,
          janus::driver::parse_semantic_version(selected)))
    throw std::runtime_error{"locked version " + selected + " of package '" +
                             dependency.name + "' no longer satisfies '" +
                             dependency.version_requirement + "'"};

  const std::filesystem::path cache =
      cache_root() / "registry" / dependency.name / selected;
  if (!std::filesystem::exists(cache)) {
    if (offline)
      throw std::runtime_error{"registry package '" + dependency.name +
                               "' is not cached and --offline was requested"};
    const std::filesystem::path source =
        package_versions / selected / "package";
    if (!std::filesystem::is_directory(source))
      throw std::runtime_error{"registry package '" + dependency.name + " " +
                               selected + "' is unavailable"};
    const std::filesystem::path staging = cache.string() + ".new";
    std::filesystem::create_directories(staging.parent_path());
    std::filesystem::remove_all(staging);
    std::filesystem::copy(source, staging,
                          std::filesystem::copy_options::recursive);
    std::filesystem::rename(staging, cache);
  }
  return cache;
}

janus::driver::Manifest
validate_dependency(const janus::driver::Dependency &dependency,
                    const std::filesystem::path &root) {
  const janus::driver::Manifest dependency_manifest =
      janus::driver::load_manifest(root / "janus.toml");
  if (dependency_manifest.name != dependency.name)
    throw std::runtime_error{"dependency '" + dependency.name +
                             "' declares package name '" +
                             dependency_manifest.name + "'"};
  if (!std::filesystem::is_directory(root / "src"))
    throw std::runtime_error{"dependency '" + dependency.name +
                             "' has no src directory"};
  return dependency_manifest;
}

} // namespace

namespace janus::driver {

std::vector<std::filesystem::path>
resolve_dependencies(const Manifest &manifest,
                     const DependencyOptions &options) {
  const std::filesystem::path lock_path = manifest.root() / "janus.lock";
  const std::map<std::string, std::string> locked =
      options.locked ? locked_versions(lock_path)
                     : std::map<std::string, std::string>{};
  std::vector<std::filesystem::path> search_paths;
  std::string lock = "# Generated by Janus. Do not edit.\nversion = 1\n";
  std::map<std::string, std::string> resolved_sources;
  std::set<std::string> visiting;

  const auto resolve = [&](const auto &self, const Manifest &owner,
                           const Dependency &dependency) -> void {
    std::filesystem::path root;
    if (dependency.is_registry()) {
      const auto found = locked.find(dependency.name);
      root = resolve_registry(dependency, options.offline,
                              found == locked.end() ? std::string{}
                                                    : found->second);
    } else if (dependency.is_git()) {
      root = resolve_git(dependency, options.offline);
    } else {
      root = (owner.root() / dependency.path).lexically_normal();
    }
    if (!std::filesystem::is_directory(root))
      throw std::runtime_error{"dependency '" + dependency.name +
                               "' does not exist at '" + root.string() + "'"};
    const std::string source_identity =
        dependency.is_registry()
            ? "registry+" +
                  std::filesystem::weakly_canonical(root).generic_string()
            : (dependency.is_git()
                   ? "git+" + dependency.git + "#" + dependency.revision
                   : "path+" + std::filesystem::weakly_canonical(root)
                                   .generic_string());
    if (visiting.contains(dependency.name))
      throw std::runtime_error{"cyclic dependency involving '" +
                               dependency.name + "'"};
    if (const auto existing = resolved_sources.find(dependency.name);
        existing != resolved_sources.end()) {
      if (existing->second != source_identity)
        throw std::runtime_error{"dependency source conflict for '" +
                                 dependency.name + "'"};
      return;
    }

    visiting.insert(dependency.name);
    const Manifest dependency_manifest = validate_dependency(dependency, root);
    const SemanticVersion resolved_version =
        parse_semantic_version(dependency_manifest.version);
    if (!matches_version(dependency.version_requirement, resolved_version))
      throw std::runtime_error{"dependency '" + dependency.name +
                               "' resolved to " + dependency_manifest.version +
                               ", which does not satisfy '" +
                               dependency.version_requirement + "'"};
    resolved_sources.emplace(dependency.name, source_identity);
    lock += "\n[[dependency]]\nname = \"" + dependency.name + "\"\n";
    if (dependency.is_registry()) {
      lock += "source = \"registry+" + dependency.name + "\"\n";
      lock += "version = \"" + dependency_manifest.version + "\"\n";
    } else if (dependency.is_git()) {
      lock += "source = \"git+" + dependency.git + "\"\n";
      lock += "revision = \"" + dependency.revision + "\"\n";
      lock += "version = \"" + dependency_manifest.version + "\"\n";
    } else {
      const std::filesystem::path project_relative =
          std::filesystem::relative(root, manifest.root());
      lock += "source = \"path+" + project_relative.generic_string() + "\"\n";
      lock += "version = \"" + dependency_manifest.version + "\"\n";
    }
    for (const Dependency &transitive : dependency_manifest.dependencies)
      self(self, dependency_manifest, transitive);
    search_paths.push_back(root / "src");
    visiting.erase(dependency.name);
  };

  for (const Dependency &dependency : manifest.dependencies)
    resolve(resolve, manifest, dependency);
  std::string existing_lock;
  {
    std::ifstream input{lock_path, std::ios::binary};
    existing_lock.assign(std::istreambuf_iterator<char>{input},
                         std::istreambuf_iterator<char>{});
  }
  if (options.locked) {
    if (existing_lock.empty())
      throw std::runtime_error{"--locked requires an existing janus.lock"};
    if (existing_lock != lock)
      throw std::runtime_error{
          "janus.lock is out of date; run without --locked to update it"};
    return search_paths;
  }
  if (existing_lock == lock)
    return search_paths;
  const std::filesystem::path temporary = manifest.root() / "janus.lock.new";
  {
    std::ofstream output{temporary, std::ios::trunc};
    if (!output)
      throw std::runtime_error{"cannot write janus.lock"};
    output << lock;
  }
  std::error_code ignored;
  std::filesystem::remove(lock_path, ignored);
  std::filesystem::rename(temporary, lock_path);
  return search_paths;
}

} // namespace janus::driver
