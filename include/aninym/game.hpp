#pragma once

#include <aninym/window.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace aninym {

enum class Runtime {
  Native,
  Wine,  // Wine, Proton, or anything else built on Wine
};

// A game that is running, as seen from outside: its processes, files and
// windows. Nothing here says which visual novel it is; that is `Matcher`'s job.
struct Game {
  // The game's processes. The one with a window comes first.
  std::vector<int> processes;

  Runtime runtime = Runtime::Native;

  // The Unix path of the program, the .exe itself for a Wine game. Of a game
  // with several processes, the program of the first, the one with a window.
  std::filesystem::path executable;

  // The program's path as Wine reports it, e.g. "C:\Games\Saya\saya.exe".
  // Empty for a native game, and for a Wine game started by its Unix path.
  std::string windows_executable;

  // The folder the game is installed in. Usually the one the program is in,
  // but folders such as "bin" or "x64" are passed over for the one above, and a
  // Steam game's is the folder Steam installed it to.
  std::filesystem::path folder;

  // The Wine prefix it runs in. Empty for a native game.
  std::filesystem::path wine_prefix;

  // The Steam app it was started as, or 0 if Steam didn't start it.
  std::uint32_t steam_app_id = 0;

  // The app's name in the Steam library's manifest, if it could be read.
  std::string steam_name;

  // The titles of its windows, in no particular order.
  std::vector<std::string> window_titles;
};

struct ScanOptions {
  // Where procfs is. Only a test would point this elsewhere.
  std::filesystem::path proc = "/proc";

  // Programs to treat as games although nothing else marks them as one. Wine
  // programs and anything Steam started are found anyway, so this is for native
  // games outside Steam, such as one a user has linked by hand.
  std::vector<std::filesystem::path> executables;

  // Whether to read window titles at all.
  bool window_titles = true;

  // Lists the windows to take titles from. Empty means the X11 display named by
  // $DISPLAY (see `x11_windows`).
  std::function<std::vector<Window>()> list_windows;
};

// The games running for the current user. Only Wine programs, programs Steam
// started and programs in `options.executables` are considered; system
// programs, Wine's own and Steam's own are left out. Processes of one game are
// grouped together.
std::vector<Game> scan(const ScanOptions& options = {});

}  // namespace aninym
