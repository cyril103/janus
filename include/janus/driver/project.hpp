#pragma once

#include <filesystem>
#include <string>

namespace janus::driver {

void create_project(const std::filesystem::path &directory,
                    const std::string &name = {});
void initialize_project(const std::filesystem::path &directory,
                        const std::string &name = {});

} // namespace janus::driver
