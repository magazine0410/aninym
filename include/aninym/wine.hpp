#pragma once

#include <filesystem>
#include <string_view>

namespace aninym {

// Whether `path` is a Windows path: "C:\..." or "\\?\...".
bool is_windows_path(std::string_view path);

// The Unix path that a Windows path refers to in a Wine prefix, through the
// drives in the prefix's dosdevices folder (C: is usually drive_c, Z: the root).
// Windows paths ignore case and Unix ones don't, so each part is matched to the
// file system's spelling where it exists. Empty when the drive isn't mapped.
std::filesystem::path unix_path(std::string_view windows_path, const std::filesystem::path& prefix);

}  // namespace aninym
