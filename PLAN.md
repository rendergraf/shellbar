# ShellBar v1.1 — Architecture Plan

> **Author:** Xavier Araque <xavieraraque@gmail.com> — May 2026
> **Repo:** [github.com/rendergraf/shellbar](https://github.com/rendergraf/shellbar)

## Vision

ShellBar is a terminal emulator based on **libghostty-vt** (Ghostty's core)
that looks and feels like Ghostty, but adds a configurable toolbar with
buttons that execute commands on the active terminal.

## Relationship with Ghostty / Ghostling

```
┌─────────────────────────────────────────────────────────┐
│  shellbar/  ← our own project, independent              │
│                                                         │
│  cmake --build .  →  ./shellbar                         │
│      │                                                  │
│      ├── uses libghostty-vt (VT emulation C library)    │
│      │     (fetched by CMake from the ghostty repo)     │
│      │                                                  │
│      └── references ghostling/main.c for patterns       │
│            (PTY, input, render)                         │
│                                                         │
│  shellbar does NOT modify ghostty/ or ghostling/        │
│  shellbar is NOT a fork — it's a new project            │
└─────────────────────────────────────────────────────────┘
```

**We never touch the `ghostty/` or `ghostling/` directories.**
Both remain clean upstream repos so we can `git pull`.

## Technology Stack

| Layer | Technology | Reason |
|-------|-----------|--------|
| Language | **C11** | Simple, portable, same as Ghostling |
| GUI | **GTK4 + libadwaita** | Same toolkit as Ghostty on Linux — identical appearance |
| Terminal core | **libghostty-vt** (Zig → .so) | Full VT engine, SIMD, scrollback, colors, Kitty protocol |
| PTY | **forkpty** (glibc) | Standard Unix, same API as Ghostling |
| Render | **Cairo** (via GtkDrawingArea) | Natural GTK4 integration, software render |
| Text | **Pango** + **FontConfig** | Anti-aliasing, unicode, font fallback |
| Build | **CMake 3.19+** + **Ninja** | Same system as Ghostling |
| Config | **key = value** (like Ghostty) | Familiar to Ghostty users |
| Icons | **SVG + GTK icon theme** | Scalable, native theme |

### Why Cairo (not OpenGL)

Software rendering with Cairo is fast enough for a terminal
(~2000–12000 cells per frame). We avoid OpenGL/GLArea complexity
in the initial version. GPU rendering can be added later.

## Project Structure

```
shellbar/
├── CMakeLists.txt          # Build system
├── shellbar.c              # Entry point (main)
├── sb_window.c / .h        # Main GTK4 window (Adw.ApplicationWindow)
├── sb_terminal.c / .h      # Terminal widget: PTY + VT + render
├── sb_toolbar.c / .h       # Command button toolbar
├── sb_config.c / .h        # Config file loading + CLI
├── PLAN.md
├── README.md
└── TODO.md
```

## Visual Appearance (Ghostty clone)

ShellBar looks almost identical to Ghostty on Linux:

```
┌──────────────────────────────────────────────────┐
│  [+] [≡]                                        │  ← AdwHeaderBar (drag handle + buttons + menu)
├──────────────────────────────────────────────────┤
│  [Terminal 1]  [Terminal 2]  [*]                │  ← AdwTabBar (standalone bar, autohide OFF)
├──────────────────────────────────────────────────┤
│ [▶ cmd1] [▶ cmd2] [▶ cmd3] [+ Add]              │  ← ShellBar Toolbar (NEW)
├──────────────────────────────────────────────────┤
│                                                  │
│  ~/projects/myapp $ pnpm storybook               │
│  > Storybook 8.0.0 starting...                   │
│  │                                                │  ← Terminal area (Ghostty identical)
│  │                                                │
│  │                                                │
├──────────────────────────────────────────────────┤
│  [scrollbar]                                      │
└──────────────────────────────────────────────────┘
```

### Key layout details:
- **HeaderBar**: custom `decoration_layout` (`:minimize,maximize,close`), no window title, new tab button and menu button on the right
- **TabBar**: standalone `AdwTabBar` as a separate top bar — NOT nested inside the header. `adw_tab_bar_set_autohide(FALSE)`. Connected to `AdwTabView` via `adw_tab_bar_set_view()`.
- **Toolbar**: our custom command buttons, added as a third top bar
- **Content**: `AdwTabView` fills the remaining space. Each page is a `GtkDrawingArea` (terminal widget).
- **TabBar was moved outside the header** because nested inside `adw_header_bar_set_title_widget()` caused zero-height allocation on some GTK4 versions.

## System Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    shellbar (main)                        │
├──────────────────────────────────────────────────────────┤
│  GTK4 / libadwaita Event Loop                             │
│                                                          │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────┐  │
│  │ sb_window    │  │ sb_toolbar   │  │ sb_terminal     │  │
│  │ (Adw.AppWin) │  │ (Gtk.Box     │  │ (Gtk.DrawingArea│  │
│  │  tabs, nav)  │  │  + buttons)  │  │  + PTY + VT)   │  │
│  └──────┬───────┘  └──────┬───────┘  └───────┬─────────┘  │
│         │                  │                  │            │
│         └──────────────────┴──────────────────┘            │
│                           │                                │
│                    ┌──────┴───────┐                        │
│                    │ sb_config    │                        │
│                    │ (key=value)  │                        │
│                    └──────────────┘                        │
├──────────────────────────────────────────────────────────┤
│                    libghostty-vt (.so)                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │ Terminal  │  │ Render   │  │ Key/Mouse│  │ Kitty    │  │
│  │ State     │  │ State    │  │ Encoders │  │ Graphics │  │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘  │
├──────────────────────────────────────────────────────────┤
│                    Operating System                        │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ forkpty → /bin/zsh or user's shell                   │  │
│  └──────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

### Command button flow

1. User clicks a toolbar button (e.g. "Storybook")
2. `sb_toolbar` captures the `clicked` signal
3. Looks up the associated command: `cd /path && pnpm storybook`
4. Gets the PTY fd of the active terminal from `sb_terminal`
5. Writes the command + `\n` to the PTY fd via `write(pty_fd, command, len)`
6. The shell receives the text as if the user typed it
7. The command output is rendered in the terminal area

## Updating from Ghostty Upstream

Since we use `libghostty-vt` via CMake FetchContent:

1. Ghostty upstream releases a new version with fixes/features
2. In `CMakeLists.txt`, change the `GIT_TAG` to the new commit
3. Delete `build/` and rebuild
4. If the `libghostty-vt` API changed, adjust our C code

**This works because:**
- `libghostty-vt` has a stable, well-documented C API
- We never modify Ghostty code
- Our code is independent of Ghostty's GUI

## Implementation Phases

### Phase 1 — Base terminal
- [x] CMake setup with libghostty-vt fetch
- [x] GTK4 window with Adw.ApplicationWindow
- [x] PTY + shell (forkpty)
- [x] Cairo terminal rendering
- [x] Keyboard and mouse input
- [x] Scrollback and scrollbar

### Phase 2 — Toolbar
- [x] GTK4 toolbar
- [x] Buttons with icons + text
- [x] Click writes command to PTY
- [x] "+" button to add commands (placeholder)

### Phase 3 — Configuration
- [x] Config file `~/.config/shellbar/config`
- [x] Parse button list from config
- [x] Icon catalog (GTK theme icons)
- [x] Hot reload

### Phase 4 — Visual polish (Ghostty clone)
- [x] Dark theme
- [x] Minimalist HeaderBar
- [x] Tabs (Adw.TabView)
- [ ] Animations and transitions
- [ ] HiDPI
- [ ] Ghostty-matching CSS

### Phase 5 — Extras
- [ ] Keyboard shortcuts for buttons
- [ ] Button groups (folders)
- [ ] Commands with variable arguments
- [ ] Split panes
