#pragma once

#include <string>
#include <string_view>
#include <vector>

// Small text helpers shared by aninym's sources. Not part of the public API.
namespace aninym::utf8 {

// Code points of UTF-8 text. A byte that doesn't start a valid sequence is
// read as U+FFFD, so stray Latin-1 can't hide the rest of a title.
std::u32string decode(std::string_view text);
void append(std::string& out, char32_t c);
std::string encode(std::u32string_view text);

// WM_NAME and other X11 STRING properties are Latin-1.
std::string from_latin1(std::string_view text);

char lower(char c);
std::string lower(std::string_view text);
bool iequals(std::string_view a, std::string_view b);
bool istarts_with(std::string_view text, std::string_view prefix);
bool iends_with(std::string_view text, std::string_view suffix);
bool icontains(std::string_view text, std::string_view part);

bool is_space(char32_t c);
// Without leading and trailing spaces, ideographic ones included.
std::string trim(std::string_view text);

std::vector<std::string> split(std::string_view text, char separator);

}  // namespace aninym::utf8
