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
- [ ] **2.4** Configurable position: top / bottom / left / right

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
- [ ] **4.5** Animations: resize overlay, new tab transition
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
- [ ] **5.5** Visual feedback when executing command (brief highlight)

## Phase 6: Extras

- [ ] **6.1** Button groups/folders
- [ ] **6.2** Commands with variables (user input)
- [ ] **6.3** Export/import configuration
- [ ] **6.4** Split panes (horizontal/vertical)
- [ ] **6.5** Terminal search (Ctrl+Shift+F)
- [ ] **6.6** Command completion notifications

## Maintenance

- [ ] **M1** Document libghostty-vt API usage
- [ ] **M2** Basic CI (build on Linux)
- [ ] **M3** Script to update libghostty-vt tag
- [ ] **M4** Changelog

---

**Legend:** `[ ]` pending · `[x]` completed · `[-]` in progress
