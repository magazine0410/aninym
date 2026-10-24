#include <aninym/matcher.hpp>
#include <string>

#include "check.hpp"

using aninym::Game;
using aninym::Matcher;
using aninym::Method;

namespace {

Matcher library() {
  Matcher matcher;
  matcher.add_title("Saya no Uta", "v97");
  matcher.add_title("沙耶の唄", "v97");
  matcher.add_title("Steins;Gate", "v2002");
  matcher.add_title("Steins;Gate 0", "v17102");
  matcher.add_title("Fate/stay night", "v11");
  matcher.add_title("Grisaia no Kajitsu", "v5154");
  matcher.add_title("Muv-Luv", "v92");
  matcher.add_title("AIR", "v36");
  return matcher;
}

std::string id(const std::optional<std::string>& found) {
  return found.value_or("none");
}

}  // namespace

TEST(titleMatchesWhole) {
  const auto matcher = library();
  CHECK_EQ(id(matcher.match_title("Saya no Uta")), std::string{"v97"});
  CHECK_EQ(id(matcher.match_title("STEINS;GATE")), std::string{"v2002"});
  CHECK_EQ(id(matcher.match_title("Steins;Gate 0")), std::string{"v17102"});
  CHECK_EQ(id(matcher.match_title("沙耶の唄")), std::string{"v97"});
}

TEST(titleMatchesThePartBeforeOrAfterASeparator) {
  const auto matcher = library();
  CHECK_EQ(id(matcher.match_title("Fate/stay night [Réalta Nua] -Ultimate Edition-")),
           std::string{"v11"});
  CHECK_EQ(id(matcher.match_title("Saya no Uta ~ The Song of Saya")), std::string{"v97"});
  CHECK_EQ(id(matcher.match_title("Grisaia no Kajitsu – LE FRUIT DE LA GRISAIA ver.1.0.3")),
           std::string{"v5154"});
  CHECK_EQ(id(matcher.match_title("Settings - Steins;Gate")), std::string{"v2002"});
  // The longest part that is a title wins.
  CHECK_EQ(id(matcher.match_title("Steins;Gate 0 - Chapter 1")), std::string{"v17102"});
}

TEST(titleDoesNotMatchAWordThatIsInIt) {
  const auto matcher = library();
  CHECK_EQ(id(matcher.match_title("Air Conditioner Simulator")), std::string{"none"});
  CHECK_EQ(id(matcher.match_title("Untitled - Notepad")), std::string{"none"});
  CHECK_EQ(id(matcher.match_title("")), std::string{"none"});
}

TEST(folderMatchesWithoutTags) {
  const auto matcher = library();
  CHECK_EQ(id(matcher.match_folder("Saya no Uta (v1.02) [Eng]")), std::string{"v97"});
  CHECK_EQ(id(matcher.match_folder("Grisaia_no_Kajitsu_English")), std::string{"v5154"});
  CHECK_EQ(id(matcher.match_folder("[age] Muv-Luv")), std::string{"v92"});
  CHECK_EQ(id(matcher.match_folder("Games")), std::string{"none"});
}

TEST(folderKeepsATitleEndingInAReleaseWord) {
  Matcher matcher;
  matcher.add_title("Ninja Final", "v1");
  CHECK_EQ(id(matcher.match_folder("Ninja Final")), std::string{"v1"});
  CHECK_EQ(id(matcher.match_folder("Ninja Final English")), std::string{"v1"});
}

TEST(firstIdAddedWinsATitle) {
  Matcher matcher;
  matcher.add_title("Kanon", "v1");
  matcher.add_title("Kanon", "v2");
  CHECK_EQ(id(matcher.match_title("Kanon")), std::string{"v1"});
}

TEST(gameMatchesInOrder) {
  auto matcher = library();
  matcher.add_steam_app(702050, "v97");
  matcher.add_executable("/games/sg/launcher.exe", "v2002");

  Game game;
  game.runtime = aninym::Runtime::Wine;
  game.steam_app_id = 702050;
  game.window_titles = {"Steins;Gate 0"};
  game.folder = "/games/Grisaia no Kajitsu";
  game.executable = "/games/sg/launcher.exe";

  auto match = matcher.match(game);
  CHECK(match.has_value());
  CHECK(match->method == Method::SteamAppId);
  CHECK_EQ(match->id, std::string{"v97"});
  CHECK_EQ(match->evidence, std::string{"702050"});

  game.steam_app_id = 0;
  match = matcher.match(game);
  CHECK(match->method == Method::WindowTitle);
  CHECK_EQ(match->id, std::string{"v17102"});

  game.window_titles = {"Untitled - Notepad"};
  match = matcher.match(game);
  CHECK(match->method == Method::FolderName);
  CHECK_EQ(match->id, std::string{"v5154"});
  CHECK_EQ(match->evidence, std::string{"Grisaia no Kajitsu"});

  game.folder = "/games/sg";
  match = matcher.match(game);
  CHECK(match->method == Method::Executable);
  CHECK_EQ(match->id, std::string{"v2002"});

  // Windows paths ignore case.
  game.executable = "/games/sg/LAUNCHER.EXE";
  CHECK(matcher.match(game).has_value());
  game.runtime = aninym::Runtime::Native;
  CHECK(!matcher.match(game).has_value());
}

TEST(steamNameMatchesWhenTheAppIdIsUnknown) {
  const auto matcher = library();
  Game game;
  game.steam_app_id = 1;
  game.steam_name = "Saya no Uta ~ The Song of Saya";
  const auto match = matcher.match(game);
  CHECK(match.has_value());
  CHECK(match->method == Method::SteamAppId);
  CHECK_EQ(match->id, std::string{"v97"});
}

TEST(orderCanBeChanged) {
  auto matcher = library();
  matcher.add_executable("/games/sg/launcher.exe", "v2002");
  matcher.set_order({Method::Executable, Method::WindowTitle});

  Game game;
  game.window_titles = {"Saya no Uta"};
  game.executable = "/games/sg/launcher.exe";
  CHECK_EQ(matcher.match(game)->id, std::string{"v2002"});

  matcher.set_order({Method::FolderName});
  CHECK(!matcher.match(game).has_value());
}

TEST(rankPutsTheClosestFirst) {
  const auto matcher = library();
  const auto candidates = matcher.rank("Steins Gate Elite");
  CHECK(!candidates.empty());
  if (candidates.empty()) return;
  CHECK_EQ(candidates.front().id, std::string{"v2002"});

  CHECK(matcher.rank("Saya no Uta Remastered").front().id == "v97");
  CHECK(matcher.rank("zzzz").empty());
  CHECK(matcher.rank("Steins", 1).size() == 1);
}

TEST(titleKeepsWhatOnlyLooksLikeAVersion) {
  Matcher matcher;
  matcher.add_title("Danganronpa", "v7014");
  matcher.add_title("Danganronpa V3", "v16280");
  CHECK_EQ(id(matcher.match_title("Danganronpa V3")), std::string{"v16280"});
  CHECK_EQ(id(matcher.match_title("Danganronpa V3 - Chapter 1")), std::string{"v16280"});
  // A version after the title still goes.
  CHECK_EQ(id(matcher.match_title("Danganronpa ver.1.02")), std::string{"v7014"});
}

TEST(aWideCharacterCountsAsTwoLetters) {
  Matcher matcher;
  matcher.add_title("月", "v1");
  matcher.add_title("月姫", "v7");
  CHECK_EQ(id(matcher.match_title("Settings - 月")), std::string{"none"});
  CHECK_EQ(id(matcher.match_title("月姫 - 体験版")), std::string{"v7"});
}
