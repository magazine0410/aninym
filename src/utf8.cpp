#include "utf8.hpp"

#include <algorithm>

namespace aninym::utf8 {

std::u32string decode(std::string_view text) {
  std::u32string out;
  out.reserve(text.size());

  for (std::size_t i = 0; i < text.size();) {
    const auto byte = static_cast<unsigned char>(text[i]);
    int length = 0;
    char32_t c = 0;
    if (byte < 0x80) {
      length = 1;
      c = byte;
    } else if ((byte & 0xE0) == 0xC0) {
      length = 2;
      c = byte & 0x1F;
    } else if ((byte & 0xF0) == 0xE0) {
      length = 3;
      c = byte & 0x0F;
    } else if ((byte & 0xF8) == 0xF0) {
      length = 4;
      c = byte & 0x07;
    }

    bool valid = length > 0 && i + length <= text.size();
    for (int k = 1; valid && k < length; ++k) {
      const auto next = static_cast<unsigned char>(text[i + k]);
      if ((next & 0xC0) != 0x80) {
        valid = false;
      } else {
        c = (c << 6) | (next & 0x3F);
      }
    }
    // Overlong forms and surrogates are as invalid as a broken sequence.
    if (valid && ((length == 2 && c < 0x80) || (length == 3 && c < 0x800) ||
                  (length == 4 && (c < 0x10000 || c > 0x10FFFF)) || (c >= 0xD800 && c <= 0xDFFF))) {
      valid = false;
    }

    if (valid) {
      out.push_back(c);
      i += length;
    } else {
      out.push_back(0xFFFD);
      ++i;
    }
  }

  return out;
}

void append(std::string& out, const char32_t c) {
  if (c < 0x80) {
    out.push_back(static_cast<char>(c));
  } else if (c < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (c >> 6)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else if (c < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (c >> 12)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (c >> 18)));
    out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  }
}

std::string encode(const std::u32string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char32_t c : text) append(out, c);
  return out;
}

std::string from_latin1(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) append(out, static_cast<unsigned char>(c));
  return out;
}

char lower(const char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lower(const std::string_view text) {
  std::string out{text};
  std::ranges::transform(out, out.begin(), [](const char c) { return lower(c); });
  return out;
}

bool iequals(const std::string_view a, const std::string_view b) {
  return std::ranges::equal(a, b, [](const char x, const char y) { return lower(x) == lower(y); });
}

bool istarts_with(const std::string_view text, const std::string_view prefix) {
  return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
}

bool iends_with(const std::string_view text, const std::string_view suffix) {
  return text.size() >= suffix.size() && iequals(text.substr(text.size() - suffix.size()), suffix);
}

bool icontains(const std::string_view text, const std::string_view part) {
  return lower(text).find(lower(part)) != std::string::npos;
}

bool is_space(const char32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || c == 0x00A0 ||
         c == 0x3000;
}

std::string trim(const std::string_view text) {
  const auto points = decode(text);
  auto first = std::ranges::find_if_not(points, is_space);
  auto last = std::find_if_not(points.rbegin(), points.rend(), is_space).base();
  if (first >= last) return {};
  return encode(std::u32string_view{&*first, static_cast<std::size_t>(last - first)});
}

std::vector<std::string> split(const std::string_view text, const char separator) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find(separator, start);
    if (end == std::string_view::npos) {
      parts.emplace_back(text.substr(start));
      break;
    }
    parts.emplace_back(text.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

}  // namespace aninym::utf8
