# ShellBar v1.0

> A command-bar terminal emulator built on Ghostty — with a configurable toolbar for launching commands.

**[github.com/rendergraf/shellbar](https://github.com/rendergraf/shellbar)**

ShellBar is a terminal emulator built on **Ghostty**'s VT engine
(`libghostty-vt`), with a configurable toolbar that lets you launch
commands with a single click.

Visually it's a Ghostty clone for Linux (GTK4 + libadwaita, dark theme,
inline tabs). The difference is the button bar that you can configure
to run any command on the active terminal.

## Features

- Full terminal with VT100-520, 256 colors, true color, Kitty protocol support
- **Configurable toolbar** with command buttons from `~/.config/shellbar/config`
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

## Requirements

| Dependency | Version | Purpose |
|------------|---------|---------|
| Zig | ≥ 0.15.2 | Build libghostty-vt |
| C compiler | C11 (gcc/clang) | Build ShellBar |
| CMake | ≥ 3.19 | Build system |
| Ninja | — | Build backend |
| GTK4 | ≥ 4.12 | GUI toolkit |
| libadwaita | ≥ 1.5 | Windows, tabs, styles |
| Pango | — | Terminal text |
| Cairo | — | Terminal rendering |
| git | — | Fetch libghostty-vt |

### Install dependencies (Debian/Ubuntu 24.04)

```sh
sudo apt install build-essential cmake ninja-build \
  libgtk-4-dev libadwaita-1-dev libpango1.0-dev \
  libcairo2-dev git
```

Zig: download from https://ziglang.org/download/ (≥ 0.15.2).

## Building

```sh
git clone <repo-url> shellbar
cd shellbar
cmake -B build -G Ninja
cmake --build build
./build/shellbar
```

Debug build: `cmake -B build -G Ninja`
Release build: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`

## Configuration

File: `~/.config/shellbar/config`

Format: `key = value` (same as Ghostty):

```ini
# Toolbar buttons
toolbar-button = name="Storybook", command="pnpm storybook\n", icon="media-playback-start"
toolbar-button = name="Build", command="pnpm build\n", icon="emblem-system"
toolbar-button = name="Test", command="pnpm test --watch\n", icon="emblem-default"
toolbar-button = name="Lint", command="pnpm lint --fix\n", icon="emblem-important"
```

If the config file doesn't exist, default buttons are used
(Storybook, Build, Test, Dev, Lint).

### Hot reload

```sh
kill -HUP $(pidof shellbar)
```

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+T` | New tab |
| `Alt+1`..`Alt+9`, `Alt+0` | Execute toolbar buttons 1–10 |


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
└── README.md            # This file
```

### Keyboard flow

1. `GtkEventControllerKey` on `AdwToolbarView` (capture phase) catches all keys
2. `Ctrl+T` → new tab; everything else is forwarded to `sb_terminal_handle_key()`
3. If the key maps to a Ghostty key (`gdk_keyval_to_ghostty`):
   - `ghostty_key_encoder_encode()` produces the escape sequence
   - The result is written to the PTY
4. If the key does **not** map (symbols like `*`, `!`, `ñ`, `ü`):
   - UTF-8 text is written directly to the PTY via `gdk_keyval_to_unicode()`
5. If the Ghostty encoder produces no output but UTF-8 text is available,
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
