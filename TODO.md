# ShellBar — TODO

> ShellBar is a tool designed to streamline how developers interact with their projects, especially in complex environments such as monorepos. It centralizes your most frequently used commands into a dedicated action bar within the shell, turning them into instant, one-click shortcuts.

## Phase 1: Base Terminal

- [x] **1.1** Create `CMakeLists.txt` with libghostty-vt fetch and GTK4 dependencies
- [x] **1.2** Create `shellbar.c` — entry point, AdwApplication init
- [x] **1.3** Create `sb_window.c/h` — Adw.ApplicationWindow, basic layout
- [x] **1.4** Create `sb_terminal.c/h` — terminal widget:
  - [x] `sb_terminal_new()` / `sb_terminal_free()`
  - [x] `sb_terminal_pty_spawn()` — forkpty with user's shell
  - [x] `sb_terminal_pty_read()` — drain PTY → ghostty_terminal_vt_write()
  - [x] `sb_terminal_pty_write()` — write to PTY (input + commands)
  - [x] `sb_terminal_render()` — Cairo draw callback for rendering cells
  - [x] `sb_terminal_resize()` — terminal resize
- [x] **1.5** Keyboard input:
  - [x] Map GTK keys → GhosttyKey
  - [x] Use ghostty_key_encoder_encode() → pty_write()
  - [x] Support Ctrl, Alt, Shift, Super
  - [x] Direct UTF-8 fallback for unmapped symbols (*, ñ, !, @, etc.)
- [x] **1.6** Mouse input:
  - [x] Click, selection (drag, double-click word, triple-click line, Shift+extend)
  - [x] Scroll via ghostty_mouse_encoder_encode()
- [x] **1.7** Scrollback:
  - [x] Mouse wheel → ghostty_terminal_scroll_viewport()
  - [x] Visual scrollbar
- [x] **1.8** Functional terminal — prompt visible, can type

## Phase 2: Toolbar

- [x] **2.1** Create `sb_toolbar.c/h`:
  - [x] Horizontal bar (Gtk.Box)
  - [x] Buttons with icon + label from config
  - [ ] Flow layout, wrap if many buttons
- [x] **2.2** Connect button → command:
  - [x] Click signal → write(pty_fd, command\n)
  - [x] Writes to the active tab's terminal
- [x] **2.3** "+" button placeholder for adding commands
- [x] **2.4** Configurable position: top / bottom / left / right

## Phase 3: Configuration

- [x] **3.1** Create `sb_config.c/h`:
  - [x] key=value parser (similar to Ghostty)
  - [x] Load from `~/.config/shellbar/config`
  - [ ] Load from `~/.config/shellbar/config.d/` (include)
- [x] **3.2** Define `SbConfigButton` struct (name, command, icon)
- [x] **3.3** Populate toolbar from config (fallback to defaults)
- [x] **3.4** Hot reload (SIGHUP → pipe → GLib IO watch)
- [ ] **3.5** Icon catalog:
  - [ ] Embedded SVG icons
  - [x] GTK theme icon support
  - [ ] Icon preview picker

## Phase 4: Visual Polish (Ghostty clone)

- [x] **4.1** Dark theme (AdwStyleManager → PREFER_DARK)
- [x] **4.2** Minimalist HeaderBar:
  - [x] New tab button `[+]` + `Ctrl+T`
  - [x] Menu (New Tab, About, Quit) with GActions + GMenu
  - [x] No window title (AdwTabBar as title_widget)
- [x] **4.3** Tabs (Adw.TabBar + Adw.TabView):
  - [x] Open/close tabs (each with its own terminal)
  - [x] Dynamic tab titles (from shell via OSC 0/2)
  - [x] Keyboard focus on terminal when switching tabs
  - [x] TabBar as standalone bar (not nested in header — fixes zero-height issue)
- [x] **4.4** Thin scrollbar on the right
- [x] **4.5** Animations: resize overlay, new tab transition
- [x] **4.6** HiDPI
- [x] **4.7** JetBrains Mono font (via Pango, fallback Monospace)
- [ ] **4.8** Ghostty-matching CSS:
  - [ ] Rounded borders
  - [ ] Refined spacing

## Phase 5: Shortcuts & UX

- [x] **5.1** Toolbar button shortcuts (Alt+1, Alt+2, ..., Alt+0)
- [x] **5.2** Configurable keybinds
- [x] **5.3** Drag & drop to reorder buttons
- [x] **5.4** Tooltip shows command on hover
- [x] **5.5** Visual feedback when executing command (brief highlight)

## Phase 6: Extras

- [x] **6.5** Terminal search (Ctrl+F) — inline search bar, match navigation, highlight with fade-out
- [ ] **6.1** Button groups/folders
- [ ] **6.2** Commands with variables (user input)
- [ ] **6.3** Export/import configuration
- [ ] **6.4** Split panes (horizontal/vertical)
- [ ] **6.6** Command completion notifications

## Phase 7: v1.7.0 Completed

- [x] **7.1** Font zoom (Ctrl+= / Ctrl+-) with zoom level chip overlay
- [x] **7.2** Visual button feedback (200ms flash on click)
- [x] **7.3** Embedded SVG drag-handle icon (GResource, no theme dependency)
- [x] **7.4** Utility bar toggle with tools SVG icon (green ON / gray OFF)
- [x] **7.5** Preferences dialog rows: flat borders, extra padding
- [x] **7.6** Drag-and-drop memory safety (weak pointer guards, deferred rebuild)
- [x] **7.7** Search match highlight with rounded green border and fade-out on click

## Phase 8: v1.9.0 Completed

- [x] **8.1** Command palette (Ctrl+P) — search and run from shell history + PATH executables
- [x] **8.2** Inline ghost-text autocomplete with command history support
- [x] **8.3** Autocomplete uses shell history first, falls back to PATH executables
- [x] **8.4** Autocomplete accepts with Right arrow only when cursor hasn't moved
- [x] **8.5** Autocomplete clears on navigation keys (Left, Up, Down, etc.)
- [x] **8.6** Configurable button bar position: top / bottom / left / right
- [x] **8.7** Preferences Settings page with button bar position dropdown
- [x] **8.8** Hot-reload support for button bar position changes
- [x] **8.9** Tab transitions — smooth slide-in animation for new tabs
- [x] **8.10** Resize overlay — terminal dimensions (cols × rows) on window resize
- [x] **8.11** Minimalist new tab button with green accent hover
- [x] **8.12** Tab pills styled as true tabs (straight top, curved bottom)
- [x] **8.13** Fixed autocomplete use-after-free causing invalid UTF-8 ghost text
- [x] **8.14** Button bar position reload preserves widget references (g_object_ref/unref)
- [x] **8.15** Tabs aligned to top edge, tighter spacing via AdwToolbarView flat style

## Maintenance

- [ ] **M1** Document libghostty-vt API usage
- [ ] **M2** Basic CI (build on Linux)
- [ ] **M3** Script to update libghostty-vt tag
- [ ] **M4** Changelog

---

**Legend:** `[ ]` pending · `[x]` completed · `[-]` in progress
