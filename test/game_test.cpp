#include <aninym/game.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "temporary.hpp"

namespace fs = std::filesystem;

using aninym::Game;
using aninym::Runtime;
using aninym::ScanOptions;
using aninym::Window;

namespace {

constexpr const char* kPreloader = "/usr/lib64/wine/x86_64-unix/wine64-preloader";

struct FakeProcess {
  int pid = 0;
  int ppid = 1;
  std::string comm;
  fs::path exe;
  std::vector<std::string> arguments;
  std::vector<std::string> environment;
  std::vector<std::string> maps;
};

std::string joined(const std::vector<std::string>& parts) {
  std::string out;
  for (const auto& part : parts) {
    out += part;
    out.push_back('\0');
  }
  return out;
}

// A procfs of made-up processes, and files for them to have open.
class FakeSystem {
public:
  FakeSystem() {
    fs::create_directories(proc());
  }

  fs::path proc() const {
    return root_.path() / "proc";
  }
  fs::path root() const {
    return root_.path();
  }

  // A file, made with its folders.
  fs::path file(const fs::path& relative) const {
    const auto path = root() / relative;
    fs::create_directories(path.parent_path());
    std::ofstream{path};
    return fs::canonical(path);
  }

  // A Wine prefix with C: and Z: drives.
  fs::path prefix() const {
    const auto path = root() / "pfx";
    if (!fs::exists(path / "dosdevices")) {
      fs::create_directories(path / "dosdevices");
      fs::create_directories(path / "drive_c" / "windows" / "system32");
      fs::create_directory_symlink("../drive_c", path / "dosdevices" / "c:");
      fs::create_directory_symlink("/", path / "dosdevices" / "z:");
    }
    return path;
  }

  void add(const FakeProcess& process) const {
    const auto dir = proc() / std::to_string(process.pid);
    fs::create_directories(dir);
    std::ofstream{dir / "stat"} << process.pid << " (" << process.comm << ") S " << process.ppid
                                << " 0 0 0 -1 4194304\n";
    // Wine pads the command line it rewrites.
    std::ofstream{dir / "cmdline", std::ios::binary} << joined(process.arguments)
                                                     << std::string(8, '\0');
    std::ofstream{dir / "environ", std::ios::binary} << joined(process.environment);
    std::ofstream maps{dir / "maps"};
    for (const auto& path : process.maps) {
      maps << "7f0000000000-7f0000001000 r--p 00000000 00:2a 12345                      " << path
           << '\n';
    }
    maps << "7ffd00000000-7ffd00021000 rw-p 00000000 00:00 0                          [stack]\n";
    fs::create_symlink(process.exe, dir / "exe");
  }

  // Wine's own programs, which every prefix runs and none is a game.
  void addWineServices(const int firstPid) const {
    const auto prefixVariable = "WINEPREFIX=" + prefix().string();
    add({firstPid,
         1,
         "wineserver",
         "/usr/bin/wineserver",
         {"/usr/bin/wineserver"},
         {prefixVariable}});
    int pid = firstPid;
    for (const char* name : {"services.exe", "explorer.exe", "winedevice.exe", "svchost.exe"}) {
      add({++pid,
           1,
           name,
           kPreloader,
           {std::string{"C:\\windows\\system32\\"} + name},
           {prefixVariable},
           {std::string{"/usr/lib64/wine/x86_64-windows/"} + name}});
    }
  }

  ScanOptions options() const {
    ScanOptions options;
    options.proc = proc();
    options.list_windows = [this] { return windows; };
    return options;
  }

  std::vector<Window> windows;

private:
  aninym::test::TemporaryDirectory root_;
};

}  // namespace

TEST(wineGameStartedByItsUnixPath) {
  const FakeSystem system;
  const auto exe = system.file("Games/Saya no Uta (v1.02) [Eng]/SayaNoUta.exe");
  system.addWineServices(100);
  system.add({200,
              1,
              "SayaNoUta.exe",
              kPreloader,
              {exe.string()},
              {"WINEPREFIX=" + system.prefix().string(), "HOME=" + system.root().string()},
              {exe.string()}});

  const auto games = aninym::scan(system.options());
  CHECK_EQ(games.size(), std::size_t{1});
  if (games.empty()) return;
  const auto& game = games.front();
  CHECK(game.runtime == Runtime::Wine);
  CHECK_EQ(game.processes, std::vector<int>{200});
  CHECK_EQ(game.executable, exe);
  CHECK_EQ(game.folder, exe.parent_path());
  CHECK_EQ(game.wine_prefix, system.prefix());
  CHECK(game.windows_executable.empty());
  CHECK_EQ(game.steam_app_id, std::uint32_t{0});
}

TEST(wineGameStartedByItsWindowsPath) {
  const FakeSystem system;
  const auto exe = system.file("pfx/drive_c/Games/Grisaia/bin/x64/Grisaia.exe");
  system.add({200,
              1,
              "Grisaia.exe",
              kPreloader,
              {"C:\\games\\GRISAIA\\bin\\x64\\grisaia.exe"},
              {"WINEPREFIX=" + system.prefix().string()}});

  const auto games = aninym::scan(system.options());
  CHECK_EQ(games.size(), std::size_t{1});
  if (games.empty()) return;
  CHECK_EQ(games.front().executable, exe);
  CHECK_EQ(games.front().windows_executable,
           std::string{"C:\\games\\GRISAIA\\bin\\x64\\grisaia.exe"});
  // "bin" and "x64" are passed over for the game's own folder.
  CHECK_EQ(games.front().folder, exe.parent_path().parent_path().parent_path());
}

TEST(wineGameFoundThroughItsMappedFile) {
  const FakeSystem system;
  const auto exe = system.file("Elsewhere/Real Folder/Game.exe");
  system.add({200,
              1,
              "Game.exe",
              kPreloader,
              {"D:\\Game.exe"},
              {"WINEPREFIX=" + system.prefix().string()},
              {"/usr/lib64/wine/x86_64-windows/kernel32.exe", exe.string()}});

  const auto games = aninym::scan(system.options());
  CHECK_EQ(games.size(), std::size_t{1});
  if (games.empty()) return;
  CHECK_EQ(games.front().executable, exe);
}

TEST(wineAndSteamProgramsAreNotGames) {
  const FakeSystem system;
  system.addWineServices(100);
  system.add({200,
              1,
              "steam.exe",
              kPreloader,
              {"C:\\Program Files (x86)\\Steam\\steam.exe"},
              {"WINEPREFIX=" + system.prefix().string()}});
  const auto exe = system.file("pfx/drive_c/windows/notepad.exe");
  system.add({201,
              1,
              "notepad.exe",
              kPreloader,
              {exe.string()},
              {"WINEPREFIX=" + system.prefix().string()}});
  // Not a Wine program at all.
  system.add({202, 1, "bash", "/usr/bin/bash", {"bash"}, {}});

  CHECK(aninym::scan(system.options()).empty());
}

TEST(protonGameHasItsSteamAppId) {
  const FakeSystem system;
  const auto library = system.root() / "Steam" / "steamapps";
  const auto exe = system.file("Steam/steamapps/common/Saya no Uta/Saya.exe");
  std::ofstream{library / "appmanifest_702050.acf"}
      << "\"AppState\"\n{\n\t\"appid\"\t\"702050\"\n\t\"name\"\t\"Saya no Uta ~ The Song of "
         "Saya\"\n}\n";

  const std::vector<std::string> environment{
      "SteamAppId=702050", "SteamGameId=702050", "WINEPREFIX=" + system.prefix().string(),
      "STEAM_COMPAT_INSTALL_PATH=" + (library / "common" / "Saya no Uta").string()};
  system.add({100, 1, "steam", "/home/me/.local/share/Steam/ubuntu12_32/steam", {"steam"}, {}});
  system.add({101,
              100,
              "reaper",
              "/home/me/.local/share/Steam/ubuntu12_32/reaper",
              {"reaper", "SteamLaunch", "AppId=702050"},
              environment});
  system.add({102, 101, "python3", "/usr/bin/python3", {"python3", "proton"}, environment});
  system.add(
      {103, 102, "steam.exe", kPreloader, {"C:\\windows\\system32\\steam.exe"}, environment});
  system.add({104, 103, "Saya.exe", kPreloader, {"Z:" + exe.string()}, environment});

  const auto games = aninym::scan(system.options());
  CHECK_EQ(games.size(), std::size_t{1});
  if (games.empty()) return;
  const auto& game = games.front();
  CHECK_EQ(game.processes, std::vector<int>{104});
  CHECK_EQ(game.steam_app_id, std::uint32_t{702050});
  CHECK_EQ(game.steam_name, std::string{"Saya no Uta ~ The Song of Saya"});
  CHECK_EQ(game.folder, library / "common" / "Saya no Uta");
  CHECK_EQ(game.executable, exe);
}

TEST(nativeSteamGame) {
  const FakeSystem system;
  const auto exe = system.file("Steam/steamapps/common/DDLC/lib/linux-x86_64/DDLC");
  system.add({100, 1, "steam", "/home/me/.local/share/Steam/ubuntu12_32/steam", {"steam"}, {}});
  system.add({101,
              100,
              "reaper",
              "/home/me/.local/share/Steam/ubuntu12_32/reaper",
              {"reaper"},
              {"SteamAppId=698780"}});
  system.add({102,
              101,
              "pressure-vessel",
              "/x/steamapps/common/SteamLinuxRuntime_sniper/pv",
              {"pv"},
              {"SteamAppId=698780"}});
  system.add({103, 102, "DDLC", exe, {exe.string()}, {"SteamAppId=698780"}});
  // Steam compiling shaders for a game it isn't running.
  system.add({104,
              100,
              "fossilize_repla",
              "/home/me/.local/share/Steam/ubuntu12_64/fossilize_replay",
              {"fossilize_replay"},
              {"SteamAppId=702050"}});

  const auto games = aninym::scan(system.options());
  CHECK_EQ(games.size(), std::size_t{1});
  if (games.empty()) return;
  CHECK(games.front().runtime == Runtime::Native);
  CHECK_EQ(games.front().steam_app_id, std::uint32_t{698780});
  CHECK_EQ(games.front().folder, system.root() / "Steam" / "steamapps" / "common" / "DDLC");
}

TEST(nonSteamGameSteamGameIdIsNotAnAppId) {
  const FakeSystem system;
  const auto exe = system.file("Games/VN/vn.exe");
  system.add({200,
              1,
              "vn.exe",
              kPreloader,
              {exe.string()},
              {"SteamAppId=0", "SteamGameId=12345678901234567890",
               "WINEPREFIX=" + system.prefix().string()}});
  const auto games = aninym::scan(system.options());
  CHECK_EQ(games.size(), std::size_t{1});
  if (!games.empty()) CHECK_EQ(games.front().steam_app_id, std::uint32_t{0});
}

TEST(nativeGameOnlyWhenLinked) {
  const FakeSystem system;
  const auto exe = system.file("Games/Katawa Shoujo/lib/py3-linux-x86_64/Katawa Shoujo");
  const auto script = system.file("Games/Other/run.sh");
  system.add({200, 1, "Katawa Shoujo", exe, {exe.string()}, {}});
  system.add({201, 1, "bash", "/usr/bin/bash", {"bash", script.string()}, {}});
  CHECK(aninym::scan(system.options()).empty());

  auto options = system.options();
  options.executables = {exe, script};
  const auto games = aninym::scan(options);
  CHECK_EQ(games.size(), std::size_t{2});
  if (games.size() != 2) return;
  CHECK(games[0].runtime == Runtime::Native);
  CHECK_EQ(games[0].folder, system.root() / "Games" / "Katawa Shoujo");
  CHECK_EQ(games[1].processes, std::vector<int>{201});
}

TEST(processesOfOneGameAreGroupedWithTheWindowFirst) {
  FakeSystem system;
  const auto launcher = system.file("Games/Muv-Luv/launcher.exe");
  const auto game = system.file("Games/Muv-Luv/muvluv.exe");
  const auto prefix = "WINEPREFIX=" + system.prefix().string();
  system.add({200, 1, "launcher.exe", kPreloader, {launcher.string()}, {prefix}});
  system.add({201, 200, "muvluv.exe", kPreloader, {game.string()}, {prefix}});
  system.windows = {{1, 201, "Muv-Luv", "muvluv.exe", "muvluv.exe"},
                    {2, 200, "", "launcher.exe", "launcher.exe"}};

  const auto games = aninym::scan(system.options());
  CHECK_EQ(games.size(), std::size_t{1});
  if (games.empty()) return;
  CHECK_EQ(games.front().processes, (std::vector<int>{201, 200}));
  CHECK_EQ(games.front().window_titles, std::vector<std::string>{"Muv-Luv"});
}

TEST(windowFoundByItsClassWhenThePidIsAnotherNamespaces) {
  FakeSystem system;
  const auto exe = system.file("Games/Saya/SayaNoUta.exe");
  system.add({200,
              1,
              "SayaNoUta.exe",
              kPreloader,
              {exe.string()},
              {"WINEPREFIX=" + system.prefix().string()}});
  system.windows = {{1, 7, "Saya no Uta", "sayanouta.exe", "sayanouta.exe"},
                    {2, 8, "Firefox", "Navigator", "firefox"}};

  const auto games = aninym::scan(system.options());
  CHECK_EQ(games.size(), std::size_t{1});
  if (games.empty()) return;
  CHECK_EQ(games.front().window_titles, std::vector<std::string>{"Saya no Uta"});

  auto options = system.options();
  options.window_titles = false;
  CHECK(aninym::scan(options).front().window_titles.empty());
}
