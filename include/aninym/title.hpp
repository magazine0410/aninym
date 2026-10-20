#pragma once

#include <string>
#include <string_view>

namespace aninym {

// A key under which two spellings of one title are equal. Lowercase, with
// spaces, punctuation and symbols removed, full-width ASCII made ASCII, accented
// Latin letters made plain and Roman numeral characters spelled out. Other
// scripts are kept as they are, so "STEINS;GATE" and "Steins Gate" agree, and
// so do "サクラノ詩 -櫻の森の上を舞う-" and "サクラノ詩　櫻の森の上を舞う".
std::string normalize(std::string_view text);

// A window title without what games add to their name in it: a leading tag
// such as "[Demo]", and trailing version numbers ("Ver.1.02", "v1.0.3"),
// build numbers and frame rates.
std::string clean_window_title(std::string_view title);

// A folder name without bracketed tags ("[Circle]", "(v1.02)", "【DL版】"),
// version numbers and trailing release words ("English", "Steam", "x64"), and
// with underscores, or dots when there are no spaces, read as spaces.
std::string clean_folder_name(std::string_view name);

}  // namespace aninym
