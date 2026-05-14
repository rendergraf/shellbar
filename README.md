# ShellBar v1.1

> **Ghostty-like terminal emulator with a workflow toolbar — for Linux.**
> ShellBar is **NOT a fork**. It uses `libghostty-vt` as a library via CMake FetchContent, maintaining complete independence from upstream Ghostty.

<p align="center">
  <a href="https://github.com/rendergraf/shellbar"><img src="https://img.shields.io/github/stars/rendergraf/shellbar" alt="GitHub Stars"></a>
  <a href="https://github.com/rendergraf/shellbar"><img src="https://img.shields.io/github/forks/rendergraf/shellbar" alt="GitHub Forks"></a>
  <a href="https://github.com/rendergraf/shellbar/blob/main/LICENSE"><img src="https://img.shields.io/github/license/rendergraf/shellbar" alt="MIT License"></a>
  <a href="https://rendergraf.github.io/shellbar/"><img src="https://img.shields.io/badge/Website-shellbar-blue" alt="Website"></a>
  <a href="https://github.com/rendergraf/shellbar/releases/latest"><img src="https://img.shields.io/github/v/release/rendergraf/shellbar" alt="Latest Release"></a>
  <img src="https://img.shields.io/badge/NOT_a_fork-independent-8257D0" alt="Not a fork">
</p>

## Screenshots

<p align="center">
  <img src="assets/screenshot-1.png" alt="ShellBar terminal with toolbar" width="48%">
  <img src="assets/screenshot-2.png" alt="ShellBar terminal with tabs" width="48%">
</p>
<p align="center">
  <img src="assets/screenshot-3.png" alt="ShellBar terminal with config" width="48%">
</p>

## Tech Stack

<p align="center">
  <img src="https://img.shields.io/badge/C-11-A8B9CC?logo=c&style=flat" alt="C11">
  <img src="https://img.shields.io/badge/GTK4-4.12+-7FE719?logo=gtk&style=flat" alt="GTK4">
  <img src="https://img.shields.io/badge/libadwaita-1.5+-9A6CEA?logo=gnome&style=flat" alt="libadwaita">
  <img src="https://img.shields.io/badge/libghostty--vt-VT_Engine-8257D0?logo=ghostty&style=flat" alt="libghostty-vt">
  <img src="https://img.shields.io/badge/Cairo-Render-F03C15?logo=cairo&style=flat" alt="Cairo">
  <img src="https://img.shields.io/badge/Pango-Text-EB5E00?logo=pango&style=flat" alt="Pango">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Zig-0.15.2+-F7A41D?logo=zig&style=flat" alt="Zig">
  <img src="https://img.shields.io/badge/CMake-3.19+-064F8C?logo=cmake&style=flat" alt="CMake">
  <img src="https://img.shields.io/badge/Ninja-Build-4D4D4D?logo=ninja&style=flat" alt="Ninja">
  <img src="https://img.shields.io/badge/Wayland-native-FFBC1F?logo=wayland&style=flat" alt="Wayland">
  <img src="https://img.shields.io/badge/X11-native-ED1944?logo=x.org&style=flat" alt="X11">
  <img src="https://img.shields.io/badge/Linux-FCC624?logo=linux&style=flat" alt="Linux">
</p>

## Description

ShellBar is a terminal emulator built on **Ghostty**'s VT engine
(`libghostty-vt`), with a configurable toolbar that lets you launch
commands with a single click. It's designed for developers who want a
fast, Ghostty-like terminal with workflow buttons at their fingertips.

**ShellBar is NOT a fork of Ghostty.** It uses `libghostty-vt` as a
library via CMake `FetchContent` — no patches, no upstream modifications,
no merge conflicts. This keeps the project independent, lightweight, and
easy to maintain while benefiting from Ghostty's industry-leading VT engine.

Visually it's a Ghostty-like terminal for Linux (GTK4 + libadwaita, dark
theme, inline tabs). The difference is the button bar that you configure
to run any command on the active terminal.

## Features

- Full terminal with VT100-520, 256 colors, true color, Kitty protocol support
- **Configurable toolbar** with command buttons from `~/.config/shellbar/config`
- **Text selection** via mouse drag with visual highlight
- **Copy / Paste** (Ctrl+Shift+C / Ctrl+Shift+V) with GTK clipboard
- **Right-click context menu**: Copy, Paste, Select All
- **Configurable keybinds** in config file
- **Tabs** with `Adw.TabBar` + `Adw.TabView`, each with its own shell
- Dynamic tab titles (OSC 0/2 from the shell)
- Visual Ghostty clone (dark theme, GTK4/libadwaita, no window title)
- Infinite scrollback (configurable)
- Cairo + Pango rendering (anti-aliasing, unicode, font fallback)
- `key = value` config format (same as Ghostty)
- Hot-reload config via `SIGHUP`
- `Ctrl+T` shortcut for new tab
- `Alt+1`..`Alt+0` shortcuts for toolbar buttons
- Wayland and X11 compatible

## Install

### Debian/Ubuntu (pre-built .deb)

```sh
curl -LO https://github.com/rendergraf/shellbar/releases/latest/download/shellbar_1.1.0_amd64.deb
sudo dpkg -i shellbar_1.1.0_amd64.deb
sudo apt-get install -f
```

### Build from source

```sh
git clone https://github.com/rendergraf/shellbar
cd shellbar
cmake -B build -G Ninja
cmake --build build
./build/shellbar
```

Release build: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`

## Requirements

| Dependency | Version | Purpose |
|------------|---------|---------|
| Zig | >= 0.15.2 | Build libghostty-vt |
| C compiler | C11 (gcc/clang) | Build ShellBar |
| CMake | >= 3.19 | Build system |
| Ninja | — | Build backend |
| GTK4 | >= 4.12 | GUI toolkit |
| libadwaita | >= 1.5 | Windows, tabs, styles |
| Pango | — | Terminal text |
| Cairo | — | Terminal rendering |
| git | — | Fetch libghostty-vt |

### Install dependencies (Debian/Ubuntu 24.04)

```sh
sudo apt install build-essential cmake ninja-build \
  libgtk-4-dev libadwaita-1-dev libpango1.0-dev \
  libcairo2-dev git
```

Zig: download from https://ziglang.org/download/ (>= 0.15.2).

## Configuration

File: `~/.config/shellbar/config`

Format: `key = value` (same as Ghostty):

```ini
# Toolbar buttons
toolbar-button = name="Storybook", command="pnpm storybook", icon="media-playback-start"
toolbar-button = name="Build", command="pnpm build", icon="emblem-system"
toolbar-button = name="Test", command="pnpm test --watch", icon="emblem-default"
toolbar-button = name="Lint", command="pnpm lint --fix", icon="emblem-important"

# Keybinds
keybind = action="copy", key="c", mods="ctrl+shift"
keybind = action="paste", key="v", mods="ctrl+shift"
keybind = action="select_all", key="a", mods="ctrl+shift"
```

If the config file doesn't exist, default buttons and keybinds are used.

### Hot reload

```sh
kill -HUP $(pidof shellbar)
```

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+T` | New tab |
| `Ctrl+Shift+C` | Copy selection |
| `Ctrl+Shift+V` | Paste |
| `Ctrl+Shift+A` | Select all |
| `Alt+1`..`Alt+9`, `Alt+0` | Execute toolbar buttons 1–10 |

### Keybind configuration

Keybinds are configurable via `~/.config/shellbar/config`:

```ini
keybind = action="copy", key="c", mods="ctrl+shift"
keybind = action="paste", key="v", mods="ctrl+shift"
keybind = action="select_all", key="a", mods="ctrl+shift"
```

Available actions: `copy`, `paste`, `select_all`.
Available mods: `ctrl`, `shift`, `alt`, `super` (combined with `+`).

### Tab architecture

- `AdwTabBar` is a standalone top bar inside `AdwToolbarView`, below the header — never hidden (`autohide = FALSE`)
- `AdwTabView` is the main content area
- `adw_tab_view_append()` adds each terminal widget as a page
- `adw_tab_bar_set_view()` links the bar to the view so pages sync automatically
- `"notify::selected-page"` updates the toolbar's active terminal on tab switch
- `"close-page"` destroys the terminal and removes the page
- Each tab has its own `forkpty` PTY, Ghostty VT state, and Cairo render surface

## Architecture

```
shellbar/
├── CMakeLists.txt       # Build: fetches libghostty-vt + GTK4 + Cairo/Pango
├── shellbar.c           # AdwApplication entry point
├── sb_window.c/h        # AdwApplicationWindow + AdwTabView + header
├── sb_terminal.c/h      # Terminal: PTY + libghostty-vt + Cairo render + input
├── sb_toolbar.c/h       # Toolbar with command buttons
├── sb_config.c/h        # Config key=value from ~/.config/shellbar/config
├── sb_preferences_dialog.c/h # Preferences dialog for editing buttons
└── README.md            # This file
```

### Keyboard flow

1. `GtkEventControllerKey` on `AdwToolbarView` (capture phase) catches all keys
2. `Ctrl+T` → new tab; `Alt+1..0` → toolbar shortcuts
3. Remaining keys are forwarded to `sb_terminal_handle_key()`
4. Configurable keybinds are checked first (copy, paste, select_all)
5. If the key maps to a Ghostty key (`gdk_keyval_to_ghostty`):
   - `ghostty_key_encoder_encode()` produces the escape sequence
   - The result is written to the PTY
6. If the key does **not** map (symbols like `*`, `!`, `ñ`, `ü`):
   - UTF-8 text is written directly to the PTY via `gdk_keyval_to_unicode()`
7. If the Ghostty encoder produces no output but UTF-8 text is available,
   the text is written directly (fallback)

This approach guarantees compatibility with all keyboard characters
while using the Ghostty encoder for control keys, function keys, and
modifier sequences.

## Author

**Xavier Araque** — xavieraraque@gmail.com — May 2026

## License

MIT © 2026 Xavier Araque

## Acknowledgments

- [Ghostty](https://github.com/ghostty-org/ghostty) — VT engine and reference
- [Ghostling](https://github.com/ghostty-org/ghostling) — architectural inspiration
