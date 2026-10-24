# aninym

*aninym* detects which visual novel is being played on Linux, whether it runs
natively, through Wine, or through Proton. Effectively, it does for visual novels what
[**anisthesia**](https://github.com/erengy/anisthesia) and
[**anitomy**](https://github.com/erengy/anitomy) do for anime episodes. However, 
aninym itself has no relation to those projects.

It works in two steps:

1. `aninym::scan()` finds the games that are running and reports what can be
   learned about each one from outside:
   - its processes and executable;
   - the folder it's installed in;
   - its Wine prefix;
   - its Steam app ID and name;
   - its window titles.
2. `aninym::Matcher` names each game, using titles, Steam app IDs and
   executables that you give it. It tries these methods in order:
   1. **Steam app ID**: the app the game was started as, looked up among
      `add_steam_app`. If that app ID is unknown, the app's name from the Steam
      library is matched like a title instead.
   2. **Window title**: matched against `add_title`.
   3. **Folder name**: also matched against `add_title`.
   4. **Executable**: as linked with `add_executable`.

You can change the order with `set_order`.

## Usage

```cpp
#include <aninym.hpp>
#include <iostream>

int main() {
  aninym::Matcher matcher;
  matcher.add_title("Saya no Uta", "v97");
  matcher.add_title("沙耶の唄", "v97");
  matcher.add_steam_app(702050, "v97");
  matcher.add_executable("/home/me/Games/Other VN/start.exe", "v1234");

  for (const auto& game : aninym::scan()) {
    if (const auto match = matcher.match(game)) {
      std::cout << game.executable << " is " << match->id << '\n';
    } else if (!game.window_titles.empty()) {
      // Nothing matched: offer the closest titles instead.
      for (const auto& candidate : matcher.rank(game.window_titles.front())) {
        std::cout << "maybe " << candidate.id << " (" << candidate.score << ")\n";
      }
    }
  }
}
```

The ids are yours; aninym only hands them back. Where they come from is up to
you. For example, VNDB can turn a Steam app ID into a visual novel through its
release `extlink` filter (`["extlink","=",["steam",702050]]`).

## How games are found

Everything comes from `/proc` and, for window titles, from X11. Only the
current user's processes are read.

- **Wine and Proton.** A process counts as a Wine program when its executable
  is one of Wine's loaders (`wine`, `wine64`, `wine-preloader`,
  `wine64-preloader`), or when its name ends in `.exe`.
  - Wine rewrites the command line to the program's path. If that is a Windows
    path, aninym turns it into a Unix one through the prefix's `dosdevices`
    drives.
  - If that path doesn't lead to a file, aninym takes the `.exe` that the
    process has mapped into memory instead.
  - Programs in `C:\windows`, Wine's own programs, and Steam's helpers (such as
    `steam.exe` and crash handlers) are left out. Wine's own are told by where
    they are, not by name, so a game's own `start.exe` or `cmd.exe` still
    counts.
- **Steam.** Steam starts every game under its `reaper` process. The way up
  to it can pass through processes of other users, such as a setuid `bwrap`
  sandbox, though only the current user's processes can be games.
  - A native game is a process below `reaper` whose executable is in
    `steamapps/common`. The Steam Linux Runtime and Proton's own programs don't
    count.
  - The app ID comes from `SteamAppId`, `STEAM_COMPAT_APP_ID` or `SteamGameId`.
    A non-Steam game added to the library has a 64-bit `SteamGameId` and no app
    ID, so it gets 0.
  - The name comes from the library's `appmanifest_<id>.acf`.
- **Other native games** are only considered when they are listed in
  `ScanOptions::executables`, such as programs a user linked by hand. This
  includes scripts: the second argument of `sh`, `bash`, `python` and similar
  interpreters is checked too.
- **The folder.** A Steam game's folder is the one Steam installed it to.
  Otherwise it is the folder the program is in, except that folders such as
  `bin`, `x64` or `lib/py3-linux-x86_64` are skipped in favour of the one above.
  A program sitting directly in `Games`, `drive_c`, `Program Files` or the home
  folder has no folder of its own.
- **Window titles** come from Xwayland, or from the X server under an X11
  session. aninym reads the window list from `_NET_CLIENT_LIST`, or from the
  window tree when no window manager keeps that list.
  - A window is matched to its game by `_NET_WM_PID`.
  - If the pid doesn't match (for example, because the game runs in a Flatpak
    with its own pid namespace), aninym falls back to `WM_CLASS`. Wine sets
    `WM_CLASS` to the program's file name.
  - aninym only connects to the display when it has found a game. This matters
    on desktops that start Xwayland on demand.

Processes of one game are grouped: by Steam app ID, otherwise by folder. The
process with a window comes first, and the game's `executable` is that
process's program, so a launcher left open beside the game doesn't stand in for
it.

## How titles are matched

Titles are compared after `normalize()`:
- spaces, punctuation and symbols are ignored, apart from `+`, which reads as
  "plus", so that "X+" isn't "X";
- case is ignored, in Latin, Greek and Cyrillic letters;
- full-width ASCII becomes ASCII, and superscript and circled digits (`²`,
  `②`) the digits they are;
- Latin letters lose their accents, as far as Latin Extended-A (`Č`, `Ł`, `Ő`).

For example, "STEINS;GATE" equals "Steins Gate". Japanese text is compared as
it is, apart from punctuation.

**Window titles** are tried as they are, and then again once they have lost a
leading `[Demo]`-style tag and any trailing version numbers, build numbers or
frame rates, so that a title such as "Danganronpa V3" keeps its "V3". Each time,
the following are tried, in order:
1. the whole title;
2. the part before each separator (`-`, `|`, `~`, `:` or an opening bracket),
   longest first;
3. the part after each separator.

For example, "Fate/stay night [Réalta Nua] -Ultimate Edition-" matches
"Fate/stay night". A part shorter than three characters never matches on its
own; a CJK character counts as two, so "月" alone doesn't, and "月姫" does.

**Folder names** first lose bracketed tags such as `[Circle]`, `(v1.02)` and
`【DL版】`. Underscores, or dots when the name has no spaces, are read as
spaces. Then trailing release words ("English", "Steam", "x64"…) and versions
are dropped one at a time, and the name is tried at each step. This way a title
that ends in such a word still matches.

When the same title belongs to more than one id, the first one added wins.

`Matcher::rank()` gives the closest titles to some text, scored from 0 to 1 by
shared character pairs, for when nothing matched.

## Limitations

- **Wayland.** Window titles are only read from X11. A game drawing through
  Wine's Wayland driver, or a native Wayland game, has no window title for
  aninym. It is still found, and can still match by Steam app ID, folder name or
  executable.
- **Flatpak.** A game in a Flatpak sandbox, or a Wine prefix in another mount
  namespace, is reported with the paths it sees, which can differ from the
  host's.
- **Other native games.** Native games outside Steam are only found when they
  are listed in `ScanOptions::executables`. Nothing else tells a native game
  apart from any other program.

## Building

aninym needs CMake 3.21 and C++20. libxcb is optional: without it, aninym
builds without window titles.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

| Option | Default | |
|---|---|---|
| `ANINYM_X11` | `ON` | Read window titles through libxcb |
| `ANINYM_BUILD_TESTS` | top level only | Build the tests |
| `ANINYM_BUILD_TOOLS` | top level only | Build `aninym-scan` |

To use it in another CMake project, add it with `add_subdirectory(aninym)` and
link `aninym::aninym`.

`aninym-scan` prints what aninym sees, and what it matches:

```sh
aninym-scan --titles titles.tsv --steam 702050=v97 --link /path/to/game.exe=v1234
```

`titles.tsv` has one title per line, after its id and a tab.

The tests build a made-up `/proc` in a temporary folder, so they don't need
Wine, Steam or a display.
