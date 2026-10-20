#include <aninym/steam.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "utf8.hpp"

namespace aninym {

namespace fs = std::filesystem;

namespace {

// The quoted string that starts at `pos` in a Valve KeyValues file, with its
// escapes undone; `pos` ends up after it.
std::string readQuoted(const std::string& text, std::size_t& pos) {
  std::string value;
  if (pos >= text.size() || text[pos] != '"') return value;
  for (++pos; pos < text.size() && text[pos] != '"'; ++pos) {
    if (text[pos] == '\\' && pos + 1 < text.size()) ++pos;
    value.push_back(text[pos]);
  }
  ++pos;
  return value;
}

void skipSpaces(const std::string& text, std::size_t& pos) {
  while (pos < text.size() &&
         (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
    ++pos;
  }
}

}  // namespace

fs::path steam_library(const fs::path& path) {
  for (auto dir = path; !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path()) {
    if (utf8::iequals(dir.filename().string(), "steamapps")) return dir;
  }
  return {};
}

std::string steam_app_name(const fs::path& path, const std::uint32_t app_id) {
  const auto library = steam_library(path);
  if (library.empty() || app_id == 0) return {};

  std::ifstream file{library / ("appmanifest_" + std::to_string(app_id) + ".acf")};
  if (!file) return {};
  std::stringstream buffer;
  buffer << file.rdbuf();
  const auto text = buffer.str();

  // "AppState" { "appid" "702050" "name" "Saya no Uta ~ The Song of Saya" ... }
  for (std::size_t pos = 0; pos < text.size();) {
    skipSpaces(text, pos);
    if (pos >= text.size()) break;
    if (text[pos] != '"') {
      ++pos;  // a brace
      continue;
    }
    const auto key = readQuoted(text, pos);
    skipSpaces(text, pos);
    if (pos < text.size() && text[pos] == '"') {
      auto value = readQuoted(text, pos);
      if (utf8::iequals(key, "name")) return value;
    }
  }
  return {};
}

}  // namespace aninym
