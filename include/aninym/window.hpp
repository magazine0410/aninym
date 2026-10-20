#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aninym {

struct Window {
  std::uint32_t id = 0;
  // From _NET_WM_PID, which is the client's own idea of its pid: it can be a pid
  // in another pid namespace (Flatpak), or missing (0).
  int pid = 0;
  std::string title;
  // The two halves of WM_CLASS. Wine sets both to the program's file name in
  // lowercase, e.g. "saya.exe", which identifies a window when the pid doesn't.
  std::string instance;
  std::string class_name;
};

// The visible top-level windows of an X11 display, `display` or else $DISPLAY.
// Under Wayland this is Xwayland, which is where Wine and Proton draw unless
// Wine's Wayland driver is in use. Empty when there is no display, or when
// aninym was built without libxcb.
//
// Connecting starts Xwayland on desktops that only start it when an X11 client
// asks for it, so `scan` only calls this when it found a game to look for.
std::vector<Window> x11_windows(const char* display = nullptr);

}  // namespace aninym
