#include <aninym/window.hpp>

#ifdef ANINYM_HAVE_XCB

#include <xcb/xcb.h>

#include <cstdlib>
#include <cstring>
#include <memory>

#include "utf8.hpp"

namespace aninym {

namespace {

template <typename T>
using Reply = std::unique_ptr<T, decltype(&std::free)>;

template <typename T>
Reply<T> reply(T* value) {
  return Reply<T>{value, &std::free};
}

struct Atoms {
  xcb_atom_t client_list = XCB_ATOM_NONE;
  xcb_atom_t pid = XCB_ATOM_NONE;
  xcb_atom_t name = XCB_ATOM_NONE;
  xcb_atom_t utf8 = XCB_ATOM_NONE;
};

Atoms internAtoms(xcb_connection_t* connection) {
  const auto ask = [connection](const char* name) {
    return xcb_intern_atom(connection, 1, static_cast<uint16_t>(std::strlen(name)), name);
  };
  // Asked for together, answered together: one round trip, not four.
  const auto client_list = ask("_NET_CLIENT_LIST");
  const auto pid = ask("_NET_WM_PID");
  const auto name = ask("_NET_WM_NAME");
  const auto utf8 = ask("UTF8_STRING");

  const auto answer = [connection](const xcb_intern_atom_cookie_t cookie) {
    const auto value = reply(xcb_intern_atom_reply(connection, cookie, nullptr));
    return value ? value->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
  };
  return {answer(client_list), answer(pid), answer(name), answer(utf8)};
}

xcb_get_property_cookie_t askProperty(xcb_connection_t* connection, const xcb_window_t window,
                                      const xcb_atom_t property) {
  return xcb_get_property(connection, 0, window, property, XCB_GET_PROPERTY_TYPE_ANY, 0, 4096);
}

std::string_view propertyText(const xcb_get_property_reply_t* value) {
  if (!value || value->format != 8) return {};
  const auto* data = static_cast<const char*>(xcb_get_property_value(value));
  return {data, static_cast<std::size_t>(xcb_get_property_value_length(value))};
}

std::vector<xcb_window_t> clientList(xcb_connection_t* connection, const xcb_window_t root,
                                     const Atoms& atoms) {
  if (atoms.client_list == XCB_ATOM_NONE) return {};
  const auto value = reply(xcb_get_property_reply(
      connection,
      xcb_get_property(connection, 0, root, atoms.client_list, XCB_GET_PROPERTY_TYPE_ANY, 0, 65536),
      nullptr));
  if (!value || value->format != 32) return {};
  const auto* data = static_cast<const xcb_window_t*>(xcb_get_property_value(value.get()));
  const auto count = static_cast<std::size_t>(xcb_get_property_value_length(value.get())) / 4;
  return {data, data + count};
}

std::vector<xcb_window_t> children(xcb_connection_t* connection, const xcb_window_t window) {
  const auto tree =
      reply(xcb_query_tree_reply(connection, xcb_query_tree(connection, window), nullptr));
  if (!tree) return {};
  const auto* data = xcb_query_tree_children(tree.get());
  return {data, data + xcb_query_tree_children_length(tree.get())};
}

std::vector<xcb_window_t> viewable(xcb_connection_t* connection,
                                   const std::vector<xcb_window_t>& windows) {
  std::vector<xcb_get_window_attributes_cookie_t> cookies;
  cookies.reserve(windows.size());
  for (const auto window : windows) {
    cookies.push_back(xcb_get_window_attributes(connection, window));
  }
  std::vector<xcb_window_t> shown;
  for (std::size_t i = 0; i < windows.size(); ++i) {
    const auto attributes = reply(xcb_get_window_attributes_reply(connection, cookies[i], nullptr));
    if (attributes && attributes->map_state == XCB_MAP_STATE_VIEWABLE) {
      shown.push_back(windows[i]);
    }
  }
  return shown;
}

// Without a window manager that keeps _NET_CLIENT_LIST, the windows are the
// root's viewable children, or theirs when a window manager frames them.
std::vector<xcb_window_t> treeWindows(xcb_connection_t* connection, const xcb_window_t root) {
  std::vector<xcb_window_t> windows;
  for (const auto top : viewable(connection, children(connection, root))) {
    windows.push_back(top);
    for (const auto inner : viewable(connection, children(connection, top))) {
      windows.push_back(inner);
    }
  }
  return windows;
}

}  // namespace

std::vector<Window> x11_windows(const char* display) {
  if (!display) display = std::getenv("DISPLAY");
  if (!display || !*display) return {};

  int screen_number = 0;
  const std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection{
      xcb_connect(display, &screen_number), &xcb_disconnect};
  if (xcb_connection_has_error(connection.get())) return {};
  auto* c = connection.get();

  auto screens = xcb_setup_roots_iterator(xcb_get_setup(c));
  for (int i = 0; i < screen_number && screens.rem; ++i) xcb_screen_next(&screens);
  if (!screens.rem) return {};
  const auto root = screens.data->root;

  const auto atoms = internAtoms(c);
  auto ids = clientList(c, root, atoms);
  if (ids.empty()) ids = treeWindows(c, root);

  struct Cookies {
    xcb_get_property_cookie_t pid, net_name, name, wm_class;
  };
  std::vector<Cookies> cookies;
  cookies.reserve(ids.size());
  for (const auto id : ids) {
    cookies.push_back({
        askProperty(c, id, atoms.pid),
        askProperty(c, id, atoms.name),
        askProperty(c, id, XCB_ATOM_WM_NAME),
        askProperty(c, id, XCB_ATOM_WM_CLASS),
    });
  }

  std::vector<Window> windows;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    Window window;
    window.id = ids[i];

    const auto pid = reply(xcb_get_property_reply(c, cookies[i].pid, nullptr));
    if (pid && pid->format == 32 && xcb_get_property_value_length(pid.get()) >= 4) {
      window.pid =
          static_cast<int>(*static_cast<const uint32_t*>(xcb_get_property_value(pid.get())));
    }

    const auto net_name = reply(xcb_get_property_reply(c, cookies[i].net_name, nullptr));
    const auto name = reply(xcb_get_property_reply(c, cookies[i].name, nullptr));
    if (const auto text = propertyText(net_name.get()); !text.empty()) {
      window.title = std::string{text};
    } else if (const auto legacy = propertyText(name.get()); !legacy.empty()) {
      // STRING is Latin-1; anything else (COMPOUND_TEXT) is read as is.
      window.title =
          name->type == XCB_ATOM_STRING ? utf8::from_latin1(legacy) : std::string{legacy};
    }

    const auto wm_class = reply(xcb_get_property_reply(c, cookies[i].wm_class, nullptr));
    if (const auto text = propertyText(wm_class.get()); !text.empty()) {
      // "instance\0class\0"
      const auto end = text.find('\0');
      window.instance = std::string{text.substr(0, end)};
      if (end != std::string_view::npos) {
        auto rest = text.substr(end + 1);
        window.class_name = std::string{rest.substr(0, rest.find('\0'))};
      }
    }

    if (window.title.empty() && window.instance.empty()) continue;
    windows.push_back(std::move(window));
  }
  return windows;
}

}  // namespace aninym

#else

namespace aninym {

std::vector<Window> x11_windows(const char*) {
  return {};
}

}  // namespace aninym

#endif
