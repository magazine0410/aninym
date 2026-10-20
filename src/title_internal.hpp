#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aninym::detail {

// The first half of `clean_folder_name`: tags in brackets removed, and
// underscores or dots read as spaces, but release words and versions kept.
std::string folder_without_tags(std::string_view name);

// The folder name without its tags, and then without each of its trailing
// release words and versions in turn: "Title Final English" is tried as that,
// then "Title Final", then "Title". The last is `clean_folder_name`.
std::vector<std::string> folder_name_stages(std::string_view name);

// Characters after which a window title or folder name often goes on with
// something other than the game's name: dashes, bars, tildes, colons and
// opening brackets.
bool is_separator(char32_t c);

}  // namespace aninym::detail
