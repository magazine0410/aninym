#include <aninym/steam.hpp>
#include <aninym/wine.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "check.hpp"
#include "temporary.hpp"

namespace fs = std::filesystem;

TEST(windowsPaths) {
  CHECK(aninym::is_windows_path("C:\\Games\\saya.exe"));
  CHECK(aninym::is_windows_path("z:/home/me/saya.exe"));
  CHECK(aninym::is_windows_path("\\\\?\\C:\\Games\\saya.exe"));
  CHECK(!aninym::is_windows_path("/home/me/saya.exe"));
  CHECK(!aninym::is_windows_path("saya.exe"));
}

TEST(unixPathFollowsTheDrives) {
  const aninym::test::TemporaryDirectory root;
  const auto prefix = root.path() / "pfx";
  fs::create_directories(prefix / "dosdevices");
  fs::create_directories(prefix / "drive_c" / "Games" / "Saya no Uta");
  std::ofstream{prefix / "drive_c" / "Games" / "Saya no Uta" / "Saya.exe"};
  fs::create_directory_symlink("../drive_c", prefix / "dosdevices" / "c:");
  fs::create_directory_symlink("/", prefix / "dosdevices" / "z:");

  const auto game = fs::canonical(prefix / "drive_c") / "Games" / "Saya no Uta" / "Saya.exe";
  CHECK_EQ(aninym::unix_path("C:\\Games\\Saya no Uta\\Saya.exe", prefix), game);
  // Windows doesn't mind the case; the file system does.
  CHECK_EQ(aninym::unix_path("c:\\GAMES\\saya NO uta\\SAYA.EXE", prefix), game);
  CHECK_EQ(aninym::unix_path("\\\\?\\C:\\Games\\Saya no Uta\\Saya.exe", prefix), game);
  CHECK_EQ(aninym::unix_path("Z:" + game.string(), prefix), game);
  CHECK_EQ(aninym::unix_path("\\\\?\\unix" + game.string(), prefix), game);
  // No such drive.
  CHECK(aninym::unix_path("D:\\Games\\saya.exe", prefix).empty());
  // A missing file still has a path, spelled as given below what exists.
  CHECK_EQ(aninym::unix_path("C:\\games\\New Game\\new.exe", prefix),
           fs::canonical(prefix / "drive_c") / "Games" / "New Game" / "new.exe");
}

TEST(steamManifestName) {
  const aninym::test::TemporaryDirectory root;
  const auto library = root.path() / "Steam" / "steamapps";
  fs::create_directories(library / "common" / "Saya no Uta");
  std::ofstream{library / "appmanifest_702050.acf"}
      << "\"AppState\"\n{\n\t\"appid\"\t\t\"702050\"\n\t\"name\"\t\t\"Saya no Uta \\\"The "
         "Song\\\"\"\n}\n";

  const auto game = library / "common" / "Saya no Uta" / "saya.exe";
  CHECK_EQ(aninym::steam_library(game), library);
  CHECK_EQ(aninym::steam_app_name(game, 702050), std::string{"Saya no Uta \"The Song\""});
  CHECK(aninym::steam_app_name(game, 1).empty());
  CHECK(aninym::steam_library(root.path()).empty());
}
