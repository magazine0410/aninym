#include <aninym/title.hpp>
#include <string>

#include "check.hpp"

using aninym::clean_folder_name;
using aninym::clean_window_title;
using aninym::normalize;

TEST(normalizeIgnoresCaseSpacesAndPunctuation) {
  CHECK_EQ(normalize("STEINS;GATE"), normalize("Steins Gate"));
  CHECK_EQ(normalize("Steins;Gate"), std::string{"steinsgate"});
  CHECK_EQ(normalize("Fate/stay night"), std::string{"fatestaynight"});
  CHECK_EQ(normalize("Ever17 -the out of infinity-"), std::string{"ever17theoutofinfinity"});
  CHECK(normalize("Steins;Gate 0") != normalize("Steins;Gate"));
}

TEST(normalizeFoldsWidthAccentsAndNumerals) {
  CHECK_EQ(normalize("ＡＴＲＩ"), std::string{"atri"});
  CHECK_EQ(normalize("Réalta Nua"), std::string{"realtanua"});
  CHECK_EQ(normalize("Shōjo"), std::string{"shojo"});
  CHECK_EQ(normalize("Rance Ⅹ"), std::string{"rancex"});
  CHECK_EQ(normalize("Little Busters! ★"), std::string{"littlebusters"});
}

TEST(normalizeKeepsJapanese) {
  CHECK_EQ(normalize("サクラノ詩 -櫻の森の上を舞う-"), normalize("サクラノ詩　櫻の森の上を舞う"));
  CHECK_EQ(normalize("「ひぐらしのなく頃に」"), std::string{"ひぐらしのなく頃に"});
  // The long vowel mark and the iteration mark are parts of words.
  CHECK_EQ(normalize("ノーゲーム・人々"), std::string{"ノーゲーム人々"});
  CHECK(normalize("ひぐらしのなく頃に").size() > 0);
}

TEST(windowTitleLosesVersionsBuildsAndFrameRates) {
  CHECK_EQ(clean_window_title("Grisaia no Kajitsu ver.1.0.3"), std::string{"Grisaia no Kajitsu"});
  CHECK_EQ(clean_window_title("Saya no Uta - Version 1.2"), std::string{"Saya no Uta"});
  CHECK_EQ(clean_window_title("Muv-Luv [v1.1]"), std::string{"Muv-Luv"});
  CHECK_EQ(clean_window_title("Katawa Shoujo | Build 1234"), std::string{"Katawa Shoujo"});
  CHECK_EQ(clean_window_title("ATRI (60 FPS)"), std::string{"ATRI"});
  CHECK_EQ(clean_window_title("[Demo] Summer Pockets"), std::string{"Summer Pockets"});
  CHECK_EQ(clean_window_title("【体験版】サクラノ詩"), std::string{"サクラノ詩"});
}

TEST(windowTitleKeepsWhatOnlyLooksLikeAVersion) {
  CHECK_EQ(clean_window_title("Steins;Gate 0"), std::string{"Steins;Gate 0"});
  CHECK_EQ(clean_window_title("Rev2"), std::string{"Rev2"});
  CHECK_EQ(clean_window_title("Ever17"), std::string{"Ever17"});
  CHECK_EQ(clean_window_title("v1.0"), std::string{"v1.0"});
}

TEST(folderNameLosesTagsAndReleaseWords) {
  CHECK_EQ(clean_folder_name("Saya no Uta (v1.02) [Eng]"), std::string{"Saya no Uta"});
  CHECK_EQ(clean_folder_name("[Frontwing] Grisaia no Kajitsu"), std::string{"Grisaia no Kajitsu"});
  CHECK_EQ(clean_folder_name("Grisaia_no_Kajitsu_English_v1.0.3"),
           std::string{"Grisaia no Kajitsu"});
  CHECK_EQ(clean_folder_name("Saya.no.Uta.v1.02"), std::string{"Saya no Uta"});
  CHECK_EQ(clean_folder_name("サクラノ詩【DL版】"), std::string{"サクラノ詩"});
  CHECK_EQ(clean_folder_name("Katawa Shoujo 1.3.1 Linux x86_64"), std::string{"Katawa Shoujo"});
}

TEST(folderNameKeepsTheTitle) {
  CHECK_EQ(clean_folder_name("Steins;Gate 0"), std::string{"Steins;Gate 0"});
  CHECK_EQ(clean_folder_name("Ever17"), std::string{"Ever17"});
  // Never down to nothing.
  CHECK_EQ(clean_folder_name("English"), std::string{"English"});
}
