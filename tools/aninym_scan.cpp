// Prints the games that are running, and which titles they match.
//
//   aninym-scan [--titles FILE] [--steam APPID=ID]... [--link PATH=ID]...
//
// FILE has one title per line, after its id and a tab: "v97<TAB>Saya no Uta".

#include <aninym.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

const char* methodName(const aninym::Method method) {
  switch (method) {
    case aninym::Method::SteamAppId:
      return "Steam app id";
    case aninym::Method::WindowTitle:
      return "window title";
    case aninym::Method::FolderName:
      return "folder name";
    case aninym::Method::Executable:
      return "linked executable";
  }
  return "?";
}

bool splitPair(const std::string_view text, std::string& key, std::string& value) {
  const auto equals = text.rfind('=');
  if (equals == std::string_view::npos) return false;
  key = text.substr(0, equals);
  value = text.substr(equals + 1);
  return !key.empty() && !value.empty();
}

int usage() {
  std::cerr << "usage: aninym-scan [--titles FILE] [--steam APPID=ID]... [--link PATH=ID]...\n";
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  aninym::Matcher matcher;
  aninym::ScanOptions options;
  bool matching = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (i + 1 >= argc) return usage();
    std::string key, value;
    if (arg == "--titles") {
      std::ifstream file{argv[++i]};
      if (!file) {
        std::cerr << "cannot read " << argv[i] << '\n';
        return 1;
      }
      for (std::string line; std::getline(file, line);) {
        const auto tab = line.find('\t');
        if (tab != std::string::npos) matcher.add_title(line.substr(tab + 1), line.substr(0, tab));
      }
    } else if (arg == "--steam" && splitPair(argv[++i], key, value)) {
      matcher.add_steam_app(static_cast<std::uint32_t>(std::stoul(key)), value);
    } else if (arg == "--link" && splitPair(argv[++i], key, value)) {
      matcher.add_executable(key, value);
      options.executables.emplace_back(key);
    } else {
      return usage();
    }
    matching = true;
  }

  const auto games = aninym::scan(options);
  if (games.empty()) {
    std::cout << "No games are running.\n";
    return 0;
  }

  for (const auto& game : games) {
    std::cout << (game.runtime == aninym::Runtime::Wine ? "Wine" : "Native") << " game, pid";
    for (const int pid : game.processes) std::cout << ' ' << pid;
    std::cout << "\n  executable:  " << game.executable.string() << '\n';
    if (!game.windows_executable.empty()) {
      std::cout << "  as Windows:  " << game.windows_executable << '\n';
    }
    if (!game.wine_prefix.empty())
      std::cout << "  prefix:      " << game.wine_prefix.string() << '\n';
    if (!game.folder.empty()) std::cout << "  folder:      " << game.folder.string() << '\n';
    if (game.steam_app_id) {
      std::cout << "  Steam app:   " << game.steam_app_id;
      if (!game.steam_name.empty()) std::cout << " (" << game.steam_name << ')';
      std::cout << '\n';
    }
    for (const auto& title : game.window_titles) std::cout << "  window:      " << title << '\n';

    if (matching) {
      if (const auto match = matcher.match(game)) {
        std::cout << "  matches:     " << match->id << ", by " << methodName(match->method) << " ("
                  << match->evidence << ")\n";
      } else {
        std::cout << "  matches:     nothing\n";
      }
    }
  }
  return 0;
}
