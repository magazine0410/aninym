#include <algorithm>
#include <aninym/wine.hpp>
#include <string>
#include <system_error>

#include "utf8.hpp"

namespace aninym {

namespace fs = std::filesystem;

namespace {

bool isLetter(const char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// The spelling the file system has for `name` in `directory`, which Windows
// would have matched whatever the case.
std::string spellingIn(const fs::path& directory, const std::string& name) {
  std::error_code error;
  if (fs::exists(directory / name, error)) return name;
  for (fs::directory_iterator it{directory, error}, end; !error && it != end; it.increment(error)) {
    const auto entry = it->path().filename().string();
    if (utf8::iequals(entry, name)) return entry;
  }
  return name;
}

}  // namespace

bool is_windows_path(const std::string_view path) {
  if (path.size() >= 3 && isLetter(path[0]) && path[1] == ':' &&
      (path[2] == '\\' || path[2] == '/')) {
    return true;
  }
  return path.starts_with("\\\\") || path.starts_with("\\??\\");
}

fs::path unix_path(std::string_view windows_path, const fs::path& prefix) {
  // \\?\C:\... and \??\C:\... are the same path in NT's spelling.
  for (const std::string_view nt : {"\\\\?\\", "\\??\\"}) {
    if (windows_path.starts_with(nt)) {
      windows_path.remove_prefix(nt.size());
      break;
    }
  }

  fs::path path;
  std::string_view rest;
  if (utf8::istarts_with(windows_path, "unix\\") || utf8::istarts_with(windows_path, "unix/")) {
    // Wine's own spelling of a Unix path that no drive covers.
    path = "/";
    rest = windows_path.substr(5);
  } else if (windows_path.size() >= 2 && isLetter(windows_path[0]) && windows_path[1] == ':') {
    const auto drive = prefix / "dosdevices" / (std::string{utf8::lower(windows_path[0])} + ":");
    std::error_code error;
    path = fs::canonical(drive, error);
    if (error) return {};
    rest = windows_path.substr(2);
  } else {
    return {};
  }

  std::string parts{rest};
  std::ranges::replace(parts, '\\', '/');
  bool exists = true;
  for (const auto& part : utf8::split(parts, '/')) {
    if (part.empty() || part == ".") continue;
    if (part == "..") {
      path = path.parent_path();
      continue;
    }
    // Once a part is missing, there is nothing below it to match against.
    const auto spelling = exists ? spellingIn(path, part) : part;
    path /= spelling;
    std::error_code error;
    exists = exists && fs::exists(path, error);
  }

  return path.lexically_normal();
}

}  // namespace aninym
