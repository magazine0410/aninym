#pragma once

// aninym: detects which visual novel is being played on Linux, natively or
// through Wine or Proton.
//
//   1. `scan` finds the games that are running.
//   2. `Matcher` names them: by Steam app id, window title, folder name, or a
//      program linked by hand, in that order.

#include <aninym/game.hpp>
#include <aninym/matcher.hpp>
#include <aninym/steam.hpp>
#include <aninym/title.hpp>
#include <aninym/window.hpp>
#include <aninym/wine.hpp>
