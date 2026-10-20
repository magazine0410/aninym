#pragma once

#include <aninym/game.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aninym {

// The ways a game can be told apart, in the order `Matcher` tries them unless
// told otherwise.
enum class Method {
  // The Steam app the game was started as, looked up in the app ids given to
  // `add_steam_app`. Failing that, the app's name in the Steam library is
  // matched like a window title.
  SteamAppId,
  // The titles of the game's windows.
  WindowTitle,
  // The name of the folder the game is installed in.
  FolderName,
  // The program itself, as linked with `add_executable`.
  Executable,
};

struct Match {
  std::string id;
  Method method = Method::SteamAppId;
  // What matched: the app id or name, the title, the folder name or the path.
  std::string evidence;
};

struct Candidate {
  std::string id;
  double score = 0.0;  // 0 to 1
};

// What the visual novels are called, and which Steam apps and programs are
// which, so that a running game can be named. Ids are the caller's own; aninym
// only hands them back.
class Matcher {
public:
  Matcher();

  // The methods to try and their order. Methods left out are not tried.
  void set_order(std::vector<Method> order);
  const std::vector<Method>& order() const;

  // A title can belong to more than one id (a remake, two novels of the same
  // name); the first one added wins, so add the likelier ones first.
  void add_title(std::string_view title, std::string_view id);
  void add_steam_app(std::uint32_t app_id, std::string_view id);
  void add_executable(const std::filesystem::path& executable, std::string_view id);
  void clear();

  // Tries each method in order and returns the first that names the game.
  std::optional<Match> match(const Game& game) const;
  std::optional<Match> match(const Game& game, Method method) const;

  // The id of the title that `text`, a window title, is: either as a whole, or
  // in the part before or after a separator such as " - ", " | ", "~" or an
  // opening bracket. Version numbers and the like are ignored.
  std::optional<std::string> match_title(std::string_view text) const;

  // The same for a folder name, which can also carry bracketed tags and
  // release words ("[Circle] Title (v1.02) [English]"), and underscores or dots
  // for spaces.
  std::optional<std::string> match_folder(std::string_view name) const;

  // The ids whose titles look most like `text`, best first, for when nothing
  // matched and someone has to choose.
  std::vector<Candidate> rank(std::string_view text, std::size_t limit = 8) const;

private:
  std::optional<std::string> find_title(const std::string& cleaned) const;

  std::vector<Method> order_;
  // Normalized title to ids, in the order they were added.
  std::unordered_map<std::string, std::vector<std::string>> titles_;
  // Every normalized title and its id, in the order they were added, for `rank`.
  std::vector<std::pair<std::string, std::string>> ranked_;
  std::unordered_map<std::uint32_t, std::string> steam_apps_;
  // Normalized path to id, and the same with the path in lowercase.
  std::unordered_map<std::string, std::string> executables_;
  std::unordered_map<std::string, std::string> executables_folded_;
};

}  // namespace aninym
