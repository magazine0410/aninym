#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <aninym/game.hpp>
#include <aninym/steam.hpp>
#include <aninym/wine.hpp>
#include <array>
#include <charconv>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

#include "utf8.hpp"

namespace aninym {

namespace fs = std::filesystem;

namespace {

using Environment = std::unordered_map<std::string, std::string>;

std::string readFile(const fs::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return {};
  std::string content;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(fd, buffer.data(), buffer.size());
    if (count <= 0) break;
    content.append(buffer.data(), static_cast<std::size_t>(count));
  }
  ::close(fd);
  return content;
}

// What is cheap to learn about every process; the rest is read only for the
// ones that might be games.
struct Process {
  int pid = 0;
  int ppid = 0;
  std::string comm;
  fs::path exe;
};

std::optional<Process> readProcess(const fs::path& dir, const int pid) {
  struct stat info{};
  if (::stat(dir.c_str(), &info) != 0 || info.st_uid != ::geteuid()) return std::nullopt;

  // "1234 (comm) S 1 ...": the name can hold spaces and parentheses, so it ends
  // at the last ')'.
  const auto stat = readFile(dir / "stat");
  const auto open = stat.find('(');
  const auto close = stat.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close < open) return std::nullopt;

  Process process;
  process.pid = pid;
  process.comm = stat.substr(open + 1, close - open - 1);
  // After the name come the state and the parent's pid.
  const std::string_view rest{stat.data() + close + 1, stat.size() - close - 1};
  const auto fields = utf8::split(utf8::trim(rest), ' ');
  if (fields.size() >= 2) {
    std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), process.ppid);
  }

  std::error_code error;
  process.exe = fs::read_symlink(dir / "exe", error);
  return process;
}

std::vector<std::string> readArguments(const fs::path& dir) {
  auto arguments = utf8::split(readFile(dir / "cmdline"), '\0');
  // Wine pads the command line it rewrites with empty arguments.
  while (!arguments.empty() && arguments.back().empty()) arguments.pop_back();
  return arguments;
}

Environment readEnvironment(const fs::path& dir) {
  Environment environment;
  for (const auto& entry : utf8::split(readFile(dir / "environ"), '\0')) {
    const auto equals = entry.find('=');
    if (equals == std::string::npos || equals == 0) continue;
    environment.emplace(entry.substr(0, equals), entry.substr(equals + 1));
  }
  return environment;
}

std::string get(const Environment& environment, const std::string& name) {
  const auto it = environment.find(name);
  return it == environment.end() ? std::string{} : it->second;
}

std::uint32_t steamAppId(const Environment& environment) {
  // SteamGameId is a 64-bit id for a non-Steam game added to the library, which
  // has no app id; those get 0 for the other two.
  for (const char* name : {"SteamAppId", "STEAM_COMPAT_APP_ID", "SteamGameId"}) {
    const auto value = get(environment, name);
    std::uint64_t id = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), id);
    if (error == std::errc{} && end == value.data() + value.size() && id > 0 && id <= UINT32_MAX) {
      return static_cast<std::uint32_t>(id);
    }
  }
  return 0;
}

// The last part of a Windows or Unix path.
std::string fileName(const std::string_view path) {
  const auto slash = path.find_last_of("/\\");
  return std::string{slash == std::string_view::npos ? path : path.substr(slash + 1)};
}

bool isWineLoader(const std::string_view name) {
  return name == "wine" || name == "wine64" || name == "wine-preloader" ||
         name == "wine64-preloader";
}

bool isWineLibrary(const std::string_view path) {
  for (const char* part : {"/lib/wine/", "/lib64/wine/", "/lib32/wine/", "/wine/x86_64-windows/",
                           "/wine/i386-windows/", "/wine/x86_64-unix/", "/wine/i386-unix/"}) {
    if (utf8::icontains(path, part)) return true;
  }
  return false;
}

// Programs that come with Wine, Proton or Steam, or that run beside a game
// without being it. Lowercase.
bool isHelperProgram(const std::string& name) {
  static constexpr std::array<std::string_view, 28> kNames{
      "conhost.exe",
      "explorer.exe",
      "iexplore.exe",
      "msiexec.exe",
      "plugplay.exe",
      "rpcss.exe",
      "rundll32.exe",
      "services.exe",
      "start.exe",
      "svchost.exe",
      "tabtip.exe",
      "winedbg.exe",
      "winedevice.exe",
      "winemenubuilder.exe",
      "wineboot.exe",
      "winecfg.exe",
      "xalia.exe",
      "steam.exe",
      "steamwebhelper.exe",
      "steamservice.exe",
      "steamerrorreporter.exe",
      "steamerrorreporter64.exe",
      "gameoverlayui.exe",
      "unitycrashhandler32.exe",
      "unitycrashhandler64.exe",
      "crashreportclient.exe",
      "cmd.exe",
      "wineconsole.exe",
  };
  const auto lower = utf8::lower(name);
  return std::ranges::find(kNames, lower) != kNames.end();
}

// A Wine program that is part of Windows rather than a game: anything in
// C:\windows, or loaded from Wine's own library folders.
bool isSystemProgram(const std::string& program, const fs::path& unixPath) {
  if (utf8::istarts_with(program, "c:\\windows\\") || utf8::istarts_with(program, "c:/windows/")) {
    return true;
  }
  const auto path = unixPath.string();
  return utf8::icontains(path, "/drive_c/windows/") || isWineLibrary(path) ||
         isWineLibrary(program) || isHelperProgram(fileName(program));
}

// The mapped .exe that is the program, from /proc/<pid>/maps. Wine maps the
// program's file, so its Unix path is there even when the command line only
// has a Windows path that doesn't lead anywhere.
fs::path mappedProgram(const fs::path& dir, const std::string& name) {
  fs::path fallback;
  for (const auto& line : utf8::split(readFile(dir / "maps"), '\n')) {
    // address perms offset dev inode path
    std::size_t pos = 0;
    for (int field = 0; field < 5 && pos != std::string::npos; ++field) {
      pos = line.find_first_not_of(' ', pos);
      pos = pos == std::string::npos ? pos : line.find(' ', pos);
    }
    if (pos == std::string::npos) continue;
    pos = line.find_first_not_of(' ', pos);
    if (pos == std::string::npos || line[pos] != '/') continue;
    std::string path = line.substr(pos);
    if (path.ends_with(" (deleted)")) path.resize(path.size() - 10);
    if (!utf8::iends_with(path, ".exe") || isWineLibrary(path)) continue;
    if (utf8::iequals(fileName(path), name)) return path;
    if (fallback.empty()) fallback = path;
  }
  return fallback;
}

// Folders that hold a game's program without being the game's own: the game
// is the one above.
bool isGenericFolder(const std::string& name) {
  static constexpr std::array<std::string_view, 30> kNames{
      "bin",
      "bin32",
      "bin64",
      "binaries",
      "win32",
      "win64",
      "windows",
      "windowsnoeditor",
      "x86",
      "x64",
      "x86_64",
      "amd64",
      "i386",
      "i686",
      "system",
      "exe",
      "release",
      "lib",
      "lib32",
      "lib64",
      "py2-linux-x86_64",
      "py3-linux-x86_64",
      "linux-x86_64",
      "linux-i686",
      "py3-windows-x86_64",
      "windows-x86_64",
      "windows-i686",
      "game",
      "program",
      "app",
  };
  const auto lower = utf8::lower(name);
  return std::ranges::find(kNames, lower) != kNames.end();
}

// Folders that hold many things, so that a program directly in one has no
// folder of its own.
bool isContainerFolder(const fs::path& dir, const fs::path& home) {
  if (dir.empty() || dir == dir.root_path() || (!home.empty() && dir == home)) return true;
  static constexpr std::array<std::string_view, 15> kNames{
      "drive_c", "program files", "program files (x86)",
      "games",   "common",        "steamapps",
      "users",   "home",          "public",
      "desktop", "downloads",     "documents",
      "appdata", "local",         "roaming",
  };
  const auto lower = utf8::lower(dir.filename().string());
  return std::ranges::find(kNames, lower) != kNames.end();
}

fs::path gameFolder(const fs::path& executable, const Environment& environment) {
  // Proton says where Steam installed the game.
  if (const fs::path install = get(environment, "STEAM_COMPAT_INSTALL_PATH"); !install.empty()) {
    const auto relative = executable.lexically_relative(install);
    if (!relative.empty() && *relative.begin() != "..") return install.lexically_normal();
  }

  // steamapps/common/<game>/...
  const auto library = steam_library(executable);
  if (!library.empty()) {
    const auto relative = executable.lexically_relative(library);
    auto part = relative.begin();
    if (part != relative.end() && utf8::iequals(part->string(), "common") &&
        ++part != relative.end() && std::next(part) != relative.end()) {
      return library / "common" / *part;
    }
  }

  auto dir = executable.parent_path();
  while (isGenericFolder(dir.filename().string()) && dir.has_parent_path() &&
         dir.parent_path() != dir) {
    dir = dir.parent_path();
  }
  fs::path home = get(environment, "HOME");
  if (home.empty()) {
    if (const char* own = std::getenv("HOME")) home = own;
  }
  return isContainerFolder(dir, home.lexically_normal()) ? fs::path{} : dir;
}

// Steam starts every game under its "reaper" process.
bool startedBySteam(const std::map<int, Process>& processes, int pid) {
  for (int depth = 0; depth < 64; ++depth) {
    const auto it = processes.find(pid);
    if (it == processes.end() || it->second.ppid == pid) return false;
    if (it->second.comm == "reaper") return true;
    pid = it->second.ppid;
  }
  return false;
}

fs::path normalPath(const fs::path& path) {
  std::error_code error;
  auto normal = fs::weakly_canonical(path, error);
  return (error ? path : normal).lexically_normal();
}

struct Candidate {
  int pid = 0;
  Game game;
};

std::optional<Candidate> wineGame(const fs::path& dir, const Process& process) {
  auto arguments = readArguments(dir);
  // Before Wine has rewritten the command line, it is "wine game.exe".
  while (!arguments.empty() && isWineLoader(fileName(arguments.front()))) {
    arguments.erase(arguments.begin());
  }
  if (arguments.empty()) return std::nullopt;
  const auto& program = arguments.front();

  const auto environment = readEnvironment(dir);
  fs::path prefix = get(environment, "WINEPREFIX");
  if (prefix.empty()) {
    const fs::path home = get(environment, "HOME");
    if (!home.empty()) prefix = home / ".wine";
  }

  fs::path executable;
  if (program.starts_with('/')) {
    executable = program;
  } else if (is_windows_path(program) && !prefix.empty()) {
    executable = unix_path(program, prefix);
  }
  if (isSystemProgram(program, executable)) return std::nullopt;

  std::error_code error;
  if (executable.empty() || !fs::exists(executable, error)) {
    if (auto mapped = mappedProgram(dir, fileName(program)); !mapped.empty()) {
      executable = std::move(mapped);
    }
  }
  if (executable.empty() || isSystemProgram(program, executable)) return std::nullopt;

  Candidate candidate;
  candidate.pid = process.pid;
  auto& game = candidate.game;
  game.runtime = Runtime::Wine;
  game.executable = normalPath(executable);
  if (is_windows_path(program)) game.windows_executable = program;
  game.wine_prefix = prefix;
  game.steam_app_id = steamAppId(environment);
  game.folder = gameFolder(game.executable, environment);
  return candidate;
}

// Programs that Steam starts on the way to a native game.
bool isSteamTool(const fs::path& exe) {
  const auto path = exe.string();
  if (!utf8::icontains(path, "/steamapps/common/")) return true;
  for (const char* part : {"/steamlinuxruntime", "/steam-runtime", "/proton", "/pressure-vessel"}) {
    if (utf8::icontains(path, part)) return true;
  }
  return false;
}

std::optional<Candidate> nativeGame(const fs::path& dir, const Process& process,
                                    const fs::path& executable, const bool steam) {
  const auto environment = readEnvironment(dir);
  Candidate candidate;
  candidate.pid = process.pid;
  auto& game = candidate.game;
  game.runtime = Runtime::Native;
  game.executable = normalPath(executable);
  game.steam_app_id = steam ? steamAppId(environment) : 0;
  game.folder = gameFolder(game.executable, environment);
  return candidate;
}

// Which of `executables` a native process is running: its program, or the
// script an interpreter runs.
std::optional<fs::path> linkedProgram(const fs::path& dir, const Process& process,
                                      const std::vector<fs::path>& executables) {
  if (executables.empty()) return std::nullopt;
  const auto exe = normalPath(process.exe);
  if (std::ranges::find(executables, exe) != executables.end()) return exe;

  const auto name = process.exe.filename().string();
  const bool interpreter = name == "sh" || name == "bash" || name == "dash" ||
                           name.starts_with("python") || name == "java" || name == "mono";
  if (!interpreter) return std::nullopt;
  const auto arguments = readArguments(dir);
  for (std::size_t i = 1; i < arguments.size() && i <= 2; ++i) {
    if (!arguments[i].starts_with('/')) continue;
    if (auto script = normalPath(arguments[i]);
        std::ranges::find(executables, script) != executables.end()) {
      return script;
    }
  }
  return std::nullopt;
}

std::string groupKey(const Game& game) {
  if (game.steam_app_id) return "steam:" + std::to_string(game.steam_app_id);
  if (!game.folder.empty()) return "folder:" + game.folder.string();
  return "exe:" + game.executable.string();
}

void addTitle(Game& game, const std::string& title) {
  if (!title.empty() && std::ranges::find(game.window_titles, title) == game.window_titles.end()) {
    game.window_titles.push_back(title);
  }
}

void attachWindows(std::vector<Game>& games, const std::vector<Window>& windows) {
  for (const auto& window : windows) {
    // The pid, unless it is from another pid namespace; then WM_CLASS, which
    // Wine sets to the program's file name.
    auto owner = std::ranges::find_if(games, [&window](const Game& game) {
      return window.pid > 0 &&
             std::ranges::find(game.processes, window.pid) != game.processes.end();
    });
    if (owner == games.end() && utf8::iends_with(window.instance, ".exe")) {
      owner = std::ranges::find_if(games, [&window](const Game& game) {
        return game.runtime == Runtime::Wine &&
               utf8::iequals(game.executable.filename().string(), window.instance);
      });
    }
    if (owner == games.end() || window.title.empty()) continue;

    addTitle(*owner, window.title);
    // The process with the window goes first.
    if (const auto it = std::ranges::find(owner->processes, window.pid);
        it != owner->processes.end()) {
      std::rotate(owner->processes.begin(), it, it + 1);
    }
  }
}

}  // namespace

std::vector<Game> scan(const ScanOptions& options) {
  std::map<int, Process> processes;
  std::error_code error;
  for (fs::directory_iterator it{options.proc, error}, end; !error && it != end;
       it.increment(error)) {
    const auto name = it->path().filename().string();
    int pid = 0;
    const auto [last, parsed] = std::from_chars(name.data(), name.data() + name.size(), pid);
    if (parsed != std::errc{} || last != name.data() + name.size() || pid <= 0) continue;
    if (auto process = readProcess(it->path(), pid)) processes.emplace(pid, std::move(*process));
  }

  std::vector<fs::path> executables;
  for (const auto& executable : options.executables) executables.push_back(normalPath(executable));

  std::vector<Game> games;
  std::map<std::string, std::size_t> groups;
  for (const auto& [pid, process] : processes) {
    const auto dir = options.proc / std::to_string(pid);
    const auto exeName = process.exe.filename().string();
    if (exeName.starts_with("wineserver") || process.comm == "wineserver") continue;

    std::optional<Candidate> candidate;
    const bool wine = isWineLoader(exeName) || utf8::iends_with(process.comm, ".exe");
    if (wine) {
      candidate = wineGame(dir, process);
    } else if (startedBySteam(processes, pid) && !isSteamTool(process.exe)) {
      candidate = nativeGame(dir, process, process.exe, true);
    } else if (const auto linked = linkedProgram(dir, process, executables)) {
      candidate = nativeGame(dir, process, *linked, false);
    }
    if (!candidate) continue;

    const auto key = groupKey(candidate->game);
    if (const auto group = groups.find(key); group != groups.end()) {
      games[group->second].processes.push_back(candidate->pid);
      continue;
    }
    candidate->game.processes.push_back(candidate->pid);
    groups.emplace(key, games.size());
    games.push_back(std::move(candidate->game));
  }

  if (games.empty()) return games;

  for (auto& game : games) {
    if (game.steam_app_id) {
      game.steam_name =
          steam_app_name(game.folder.empty() ? game.executable : game.folder, game.steam_app_id);
    }
  }

  if (options.window_titles) {
    attachWindows(games, options.list_windows ? options.list_windows() : x11_windows());
  }

  return games;
}

}  // namespace aninym
