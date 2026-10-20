#include <algorithm>
#include <aninym/title.hpp>
#include <array>
#include <regex>
#include <string_view>
#include <vector>

#include "title_internal.hpp"
#include "utf8.hpp"

namespace aninym {

namespace {

// U+00C0 to U+00FF without their accents; empty for × and ÷.
constexpr std::array<std::string_view, 64> kLatin1{
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i",  "i",
    "d", "n", "o", "o", "o", "o", "o",  "",  "o", "u", "u", "u", "u", "y", "th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i",  "i",
    "d", "n", "o", "o", "o", "o", "o",  "",  "o", "u", "u", "u", "u", "y", "th", "y",
};

// Ⅰ to Ⅻ, and ⅰ to ⅻ.
constexpr std::array<std::string_view, 12> kRomanNumerals{
    "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii",
};

bool isDropped(const char32_t c) {
  // Kept from the CJK punctuation block: the iteration marks 々 and 〆, and 〇,
  // which are parts of words.
  if (c == 0x3005 || c == 0x3006 || c == 0x3007) return false;
  return (c >= 0x0080 && c <= 0x00BF) ||  // Latin-1 controls and punctuation
         c == 0x00D7 || c == 0x00F7 ||    // × ÷
         (c >= 0x0300 && c <= 0x036F) ||  // combining accents
         (c >= 0x2000 && c <= 0x215F) ||  // punctuation, currency, letterlike
         (c >= 0x2180 && c <= 0x2BFF) ||  // arrows, maths, shapes, symbols
         (c >= 0x3000 && c <= 0x303F) ||  // CJK punctuation
         c == 0x30FB ||                   // katakana middle dot
         (c >= 0xE000 && c <= 0xF8FF) ||  // private use
         (c >= 0xFE00 && c <= 0xFE6F) ||  // variation selectors, small forms
         c == 0xFF00 || (c >= 0xFF5F && c <= 0xFF65) || (c >= 0xFFE0 && c <= 0xFFFF) ||
         (c >= 0x1F000 && c <= 0x1FAFF);  // emoji and other pictographs
}

void appendFolded(std::string& out, char32_t c) {
  // Full-width ASCII.
  if (c >= 0xFF01 && c <= 0xFF5E) c -= 0xFEE0;

  if (c < 0x80) {
    const auto ascii = static_cast<char>(c);
    if ((ascii >= '0' && ascii <= '9') || (ascii >= 'a' && ascii <= 'z')) {
      out.push_back(ascii);
    } else if (ascii >= 'A' && ascii <= 'Z') {
      out.push_back(utf8::lower(ascii));
    }
    return;
  }
  if (c >= 0x00C0 && c <= 0x00FF) {
    out += kLatin1[c - 0x00C0];
    return;
  }
  switch (c) {
    case 0x0100:
    case 0x0101:
      out += 'a';
      return;  // Ā ā
    case 0x0112:
    case 0x0113:
      out += 'e';
      return;  // Ē ē
    case 0x012A:
    case 0x012B:
      out += 'i';
      return;  // Ī ī
    case 0x014C:
    case 0x014D:
      out += 'o';
      return;  // Ō ō
    case 0x016A:
    case 0x016B:
      out += 'u';
      return;  // Ū ū
    case 0x0152:
    case 0x0153:
      out += "oe";
      return;  // Œ œ
    default:
      break;
  }
  if (c >= 0x2160 && c <= 0x216B) {
    out += kRomanNumerals[c - 0x2160];
    return;
  }
  if (c >= 0x2170 && c <= 0x217B) {
    out += kRomanNumerals[c - 0x2170];
    return;
  }
  if (isDropped(c)) return;

  utf8::append(out, c);
}

bool isTrailingSeparator(const char32_t c) {
  return utf8::is_space(c) || c == '-' || c == '|' || c == ':' || c == '~' || c == 0x2013 ||
         c == 0x2014 || c == 0xFF5E || c == 0x301C;
}

std::string trimSeparators(const std::string_view text) {
  auto points = utf8::decode(text);
  while (!points.empty() && isTrailingSeparator(points.back())) points.pop_back();
  auto first = std::ranges::find_if_not(points, isTrailingSeparator);
  return utf8::encode(std::u32string_view{points}.substr(first - points.begin()));
}

// Removes what one of `patterns` matches at the end of `text`, for as long as
// one does.
std::string removeTrailing(std::string text, const std::vector<std::regex>& patterns) {
  for (bool removed = true; removed;) {
    removed = false;
    for (const auto& pattern : patterns) {
      std::smatch match;
      if (std::regex_search(text, match, pattern) && match.position(0) > 0) {
        text = trimSeparators(text.substr(0, match.position(0)));
        removed = true;
      }
    }
  }
  return text;
}

const std::vector<std::regex>& windowTitleJunk() {
  constexpr auto flags = std::regex::ECMAScript | std::regex::icase;
  // Each starts at a space, a separator or a bracket, so that a word that only
  // ends in "v2" or "fps" stays whole.
  static const std::vector<std::regex> patterns{
      // Ver.1.02, Version 1.2, v1.0.3, [v1.1]
      std::regex{
          R"((?:^|[\s\-|:~\(\[])\s*(?:ver(?:sion)?\.?\s*|v\.?)\d+(?:[._]\d+)*[a-z]?\s*[\)\]]?\s*$)",
          flags},
      // Build 1234, (build #12)
      std::regex{R"((?:^|[\s\-|:~\(\[])\s*build\s*#?\s*\d+\s*[\)\]]?\s*$)", flags},
      // FPS: 60, 59.9 fps
      std::regex{
          R"((?:^|[\s\-|:~\(\[])\s*(?:fps\s*[:=]?\s*\d+(?:\.\d+)?|\d+(?:\.\d+)?\s*fps)\s*[\)\]]?\s*$)",
          flags},
  };
  return patterns;
}

bool isVersion(const std::string_view word) {
  static const std::regex pattern{
      R"(^(?:(?:ver(?:sion)?\.?|v\.?)\d+(?:[._]\d+)*[a-z]?|\d+(?:\.\d+)+[a-z]?)$)",
      std::regex::ECMAScript | std::regex::icase};
  return std::regex_match(word.begin(), word.end(), pattern);
}

bool isReleaseWord(const std::string_view word) {
  static constexpr std::array<std::string_view, 44> kWords{
      "english", "eng",  "en",         "japanese",    "jpn",      "jap",        "jp",      "ja",
      "chinese", "chs",  "cht",        "zh",          "patch",    "patched",    "steam",   "gog",
      "dlsite",  "dmm",  "fanza",      "repack",      "portable", "win",        "windows", "linux",
      "mac",     "x86",  "x64",        "x86_64",      "amd64",    "32bit",      "64bit",   "32-bit",
      "64-bit",  "full", "translated", "translation", "tl",       "uncensored", "final",   "ver",
      "version", "-",    "~",          "+",
  };
  return std::ranges::any_of(kWords, [word](const auto w) { return utf8::iequals(word, w); });
}

// Brackets that tag a folder name; the value is the closing one.
char32_t closingBracket(const char32_t c) {
  switch (c) {
    case '[':
      return ']';
    case '(':
      return ')';
    case '{':
      return '}';
    case 0x3010:
      return 0x3011;  // 【】
    case 0x3014:
      return 0x3015;  // 〔〕
    case 0xFF08:
      return 0xFF09;  // （）
    case 0xFF3B:
      return 0xFF3D;  // ［］
    default:
      return 0;
  }
}

std::string withoutBracketedTags(const std::string_view text) {
  const auto points = utf8::decode(text);
  std::u32string out;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const char32_t closing = closingBracket(points[i]);
    if (!closing) {
      out.push_back(points[i]);
      continue;
    }
    int depth = 0;
    std::size_t end = i;
    for (; end < points.size(); ++end) {
      if (points[end] == points[i]) ++depth;
      if (points[end] == closing && --depth == 0) break;
    }
    if (end == points.size()) {
      out.push_back(points[i]);  // unclosed, so not a tag
      continue;
    }
    out.push_back(' ');
    i = end;
  }
  return utf8::encode(out);
}

std::string collapseSpaces(const std::string_view text) {
  std::string out;
  bool space = false;
  for (const char32_t c : utf8::decode(text)) {
    if (utf8::is_space(c)) {
      space = true;
      continue;
    }
    if (space && !out.empty()) out.push_back(' ');
    space = false;
    utf8::append(out, c);
  }
  return out;
}

}  // namespace

namespace detail {

bool is_separator(const char32_t c) {
  switch (c) {
    case '-':
    case '|':
    case '~':
    case ':':
    case '(':
    case '[':
    case '{':
    case '<':
    case 0x00AB:  // «
    case 0x2013:
    case 0x2014:
    case 0x2015:
    case 0x2212:  // – — ― −
    case 0x2022:  // •
    case 0x3008:
    case 0x300A:
    case 0x300C:
    case 0x300E:
    case 0x3010:
    case 0x3014:  // 〈《「『【〔
    case 0x301C:  // 〜
    case 0xFF08:
    case 0xFF0D:
    case 0xFF1A:
    case 0xFF3B:
    case 0xFF5C:
    case 0xFF5E:  // （－：［｜～
      return true;
    default:
      return false;
  }
}

std::string folder_without_tags(const std::string_view name) {
  std::string text = withoutBracketedTags(name);

  // Underscores or dots for spaces, as in "Saya_no_Uta" or "Saya.no.Uta.v1.02",
  // but not the dots in a number. A name that has spaces uses them, so its
  // underscores are its own, as in "x86_64".
  if (utf8::trim(text).find(' ') == std::string::npos) {
    std::ranges::replace(text, '_', ' ');
  }
  if (utf8::trim(text).find(' ') == std::string::npos) {
    const auto isDigit = [](const char c) { return c >= '0' && c <= '9'; };
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (text[i] != '.') continue;
      const bool inNumber =
          i > 0 && i + 1 < text.size() && isDigit(text[i - 1]) && isDigit(text[i + 1]);
      if (!inNumber) text[i] = ' ';
    }
  }

  return trimSeparators(collapseSpaces(text));
}

std::vector<std::string> folder_name_stages(const std::string_view name) {
  auto words = utf8::split(folder_without_tags(name), ' ');
  std::erase(words, std::string{});

  const auto join = [&words]() {
    std::string out;
    for (const auto& word : words) {
      if (!out.empty()) out.push_back(' ');
      out += word;
    }
    return trimSeparators(out);
  };

  std::vector<std::string> stages{join()};
  while (words.size() > 1 && (isReleaseWord(words.back()) || isVersion(words.back()))) {
    words.pop_back();
    if (auto stage = join(); stage != stages.back()) stages.push_back(std::move(stage));
  }
  return stages;
}

}  // namespace detail

std::string normalize(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char32_t c : utf8::decode(text)) appendFolded(out, c);
  return out;
}

std::string clean_window_title(const std::string_view title) {
  std::string text = utf8::trim(title);

  // A leading tag, as in "[Demo] Title" or "【体験版】Title".
  for (;;) {
    const auto points = utf8::decode(text);
    if (points.empty() || (points.front() != '[' && points.front() != 0x3010)) break;
    const char32_t closing = points.front() == '[' ? ']' : 0x3011;
    const auto end = points.find(closing);
    if (end == std::u32string::npos) break;
    auto rest = utf8::trim(utf8::encode(std::u32string_view{points}.substr(end + 1)));
    if (rest.empty()) break;
    text = std::move(rest);
  }

  return removeTrailing(trimSeparators(text), windowTitleJunk());
}

std::string clean_folder_name(const std::string_view name) {
  return detail::folder_name_stages(name).back();
}

}  // namespace aninym
