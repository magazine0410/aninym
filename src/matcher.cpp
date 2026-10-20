#include <algorithm>
#include <aninym/matcher.hpp>
#include <aninym/title.hpp>
#include <set>
#include <system_error>
#include <utility>

#include "title_internal.hpp"
#include "utf8.hpp"

namespace aninym {

namespace fs = std::filesystem;

namespace {

// A part of a title shorter than this is too likely to be a word that only
// happens to be a title too.
constexpr std::size_t kMinimumPartLength = 3;

std::string pathKey(const fs::path& path) {
  std::error_code error;
  auto normal = fs::weakly_canonical(path, error);
  return (error ? path : normal).lexically_normal().string();
}

// The ways `text` can be read as a title, best first: as a whole, then the
// parts before each separator, longest first, then the parts after one.
std::vector<std::string> titleParts(const std::string& text) {
  std::vector<std::string> parts{text};
  const auto points = utf8::decode(text);
  std::vector<std::size_t> separators;
  for (std::size_t i = 0; i < points.size(); ++i) {
    if (detail::is_separator(points[i])) separators.push_back(i);
  }
  const std::u32string_view view{points};
  for (auto it = separators.rbegin(); it != separators.rend(); ++it) {
    parts.push_back(utf8::trim(utf8::encode(view.substr(0, *it))));
  }
  for (const auto separator : separators) {
    parts.push_back(utf8::trim(utf8::encode(view.substr(separator + 1))));
  }
  return parts;
}

// Pairs of adjacent characters, which compare short titles and Japanese ones
// better than triples do.
std::set<std::u32string> bigrams(const std::string& normalized) {
  const auto points = utf8::decode(normalized);
  std::set<std::u32string> pairs;
  if (points.size() == 1) pairs.insert(points);
  for (std::size_t i = 0; i + 1 < points.size(); ++i) pairs.insert(points.substr(i, 2));
  return pairs;
}

double similarity(const std::string& a, const std::set<std::u32string>& aPairs,
                  const std::string& b) {
  const auto bPairs = bigrams(b);
  if (aPairs.empty() || bPairs.empty()) return 0.0;
  std::size_t shared = 0;
  for (const auto& pair : aPairs) shared += bPairs.contains(pair);
  // Dice's coefficient.
  double score =
      2.0 * static_cast<double>(shared) / static_cast<double>(aPairs.size() + bPairs.size());

  // One title inside the other, as with a subtitle the window has and the list
  // doesn't, is closer than the shared pairs alone say.
  const auto& shorter = a.size() < b.size() ? a : b;
  const auto& longer = a.size() < b.size() ? b : a;
  if (shorter.size() >= kMinimumPartLength && longer.find(shorter) != std::string::npos) {
    const double ratio = static_cast<double>(shorter.size()) / static_cast<double>(longer.size());
    score = std::max(score, 0.6 + 0.4 * ratio);
  }
  return score;
}

}  // namespace

Matcher::Matcher()
    : order_{Method::SteamAppId, Method::WindowTitle, Method::FolderName, Method::Executable} {}

void Matcher::set_order(std::vector<Method> order) {
  order_ = std::move(order);
}

const std::vector<Method>& Matcher::order() const {
  return order_;
}

void Matcher::add_title(const std::string_view title, const std::string_view id) {
  const auto key = normalize(title);
  if (key.empty()) return;
  auto& ids = titles_[key];
  if (std::ranges::find(ids, id) == ids.end()) {
    ids.emplace_back(id);
    ranked_.emplace_back(key, std::string{id});
  }
}

void Matcher::add_steam_app(const std::uint32_t app_id, const std::string_view id) {
  if (app_id) steam_apps_.emplace(app_id, std::string{id});
}

void Matcher::add_executable(const fs::path& executable, const std::string_view id) {
  if (executable.empty()) return;
  const auto key = pathKey(executable);
  executables_.emplace(key, std::string{id});
  executables_folded_.emplace(utf8::lower(key), std::string{id});
}

void Matcher::clear() {
  titles_.clear();
  ranked_.clear();
  steam_apps_.clear();
  executables_.clear();
  executables_folded_.clear();
}

std::optional<Match> Matcher::match(const Game& game) const {
  for (const auto method : order_) {
    if (auto found = match(game, method)) return found;
  }
  return std::nullopt;
}

std::optional<Match> Matcher::match(const Game& game, const Method method) const {
  switch (method) {
    case Method::SteamAppId: {
      if (!game.steam_app_id) return std::nullopt;
      if (const auto it = steam_apps_.find(game.steam_app_id); it != steam_apps_.end()) {
        return Match{it->second, method, std::to_string(game.steam_app_id)};
      }
      if (auto id = match_title(game.steam_name)) {
        return Match{std::move(*id), method, game.steam_name};
      }
      return std::nullopt;
    }
    case Method::WindowTitle:
      for (const auto& title : game.window_titles) {
        if (auto id = match_title(title)) return Match{std::move(*id), method, title};
      }
      return std::nullopt;
    case Method::FolderName: {
      const auto name = game.folder.filename().string();
      if (auto id = match_folder(name)) return Match{std::move(*id), method, name};
      return std::nullopt;
    }
    case Method::Executable: {
      if (game.executable.empty()) return std::nullopt;
      const auto key = pathKey(game.executable);
      if (const auto it = executables_.find(key); it != executables_.end()) {
        return Match{it->second, method, game.executable.string()};
      }
      // A Windows program can be named in any case.
      if (game.runtime == Runtime::Wine) {
        if (const auto it = executables_folded_.find(utf8::lower(key));
            it != executables_folded_.end()) {
          return Match{it->second, method, game.executable.string()};
        }
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<std::string> Matcher::find_title(const std::string& cleaned) const {
  const auto parts = titleParts(cleaned);
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const auto key = normalize(parts[i]);
    if (key.empty() || (i > 0 && key.size() < kMinimumPartLength)) continue;
    if (const auto it = titles_.find(key); it != titles_.end()) return it->second.front();
  }
  return std::nullopt;
}

std::optional<std::string> Matcher::match_title(const std::string_view text) const {
  if (text.empty()) return std::nullopt;
  return find_title(clean_window_title(text));
}

std::optional<std::string> Matcher::match_folder(const std::string_view name) const {
  if (name.empty()) return std::nullopt;
  for (const auto& stage : detail::folder_name_stages(name)) {
    if (auto id = find_title(stage)) return id;
  }
  return std::nullopt;
}

std::vector<Candidate> Matcher::rank(const std::string_view text, const std::size_t limit) const {
  const auto query = normalize(clean_folder_name(clean_window_title(text)));
  if (query.empty()) return {};
  const auto pairs = bigrams(query);

  std::vector<Candidate> candidates;
  for (const auto& [title, id] : ranked_) {
    const double score = similarity(query, pairs, title);
    if (score < 0.3) continue;
    const auto it = std::ranges::find(candidates, id, &Candidate::id);
    if (it == candidates.end()) {
      candidates.push_back({id, score});
    } else {
      it->score = std::max(it->score, score);
    }
  }

  // Stable, so that ids scoring the same stay in the order they were added.
  std::ranges::stable_sort(candidates, std::ranges::greater{}, &Candidate::score);
  if (candidates.size() > limit) candidates.resize(limit);
  return candidates;
}

}  // namespace aninym
