#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace aninym {

// The steamapps folder of the Steam library that `path` is in, or empty.
std::filesystem::path steam_library(const std::filesystem::path& path);

// The name of `app_id` in the manifest of the Steam library that `path` is in,
// e.g. "Saya no Uta ~ The Song of Saya". Empty when there is no manifest.
std::string steam_app_name(const std::filesystem::path& path, std::uint32_t app_id);

}  // namespace aninym
