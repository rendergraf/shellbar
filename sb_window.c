/*
 * ShellBar v1.7.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#include "sb_window.h"
#include "sb_toolbar.h"
#include "sb_config.h"
#include "sb_preferences_dialog.h"
#include "sb_theme.h"

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/* Tab tracking                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
  SbTerminal *terminal;
  AdwTabPage *page;
  GtkWidget *tab_button;
  GtkWidget *tab_close_btn;
  GtkWidget *tab_row;
} SbTabEntry;

/* ------------------------------------------------------------------ */
/* Window struct                                                       */
/* ------------------------------------------------------------------ */

#define MAX_SHORTCUTS 10

struct _SbWindow {
  AdwApplicationWindow parent;
  SbToolbar *toolbar;
  AdwTabView *tab_view;
  GtkWidget *tab_bar_box;
  GtkWidget *tab_bar_scroll;
  GtkWidget *tab_add_btn;
  GtkWidget *toolbar_view;
  GtkWidget *toolbar_revealer;
  GtkWidget *toolbar_toggle_da;
  GtkWidget *util_bar_revealer;
  GtkWidget *util_bar_box;
  GtkWidget *util_toggle_img;
  SbTabEntry *tabs;
  int tab_count;
  int tab_capacity;
  char *shortcut_cmds[MAX_SHORTCUTS];
  int shortcut_count;
  SbConfigKeybind *config_keybinds;
  int config_keybind_count;
};

G_DEFINE_TYPE(SbWindow, sb_window, ADW_TYPE_APPLICATION_WINDOW)

/* ---- Default buttons ---- */

static const SbToolbarButtonDef default_buttons[] = {
  { "Storybook", "pnpm storybook\n", "media-playback-start" },
  { "Build",     "pnpm build\n",     "emblem-system" },
  { "Test",      "pnpm test\n",      "emblem-default" },
  { "Dev",       "pnpm dev\n",       "computer" },
  { "Lint",      "pnpm lint\n",      "emblem-important" },
};
static const int default_button_count =
  sizeof(default_buttons) / sizeof(default_buttons[0]);

/* ------------------------------------------------------------------ */

static int tab_index_of(SbWindow *self, AdwTabPage *page) {
  for (int i = 0; i < self->tab_count; i++)
    if (self->tabs[i].page == page) return i;
  return -1;
}

static SbTerminal *active_terminal(SbWindow *self) {
  AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
  if (!page) return NULL;
  int idx = tab_index_of(self, page);
  return (idx >= 0) ? self->tabs[idx].terminal : NULL;
}

/* ---- Title callback from terminal → tab label ---- */

static void on_terminal_title(SbTerminal *terminal, const char *title,
                              void *userdata) {
  SbWindow *self = userdata;
  for (int i = 0; i < self->tab_count; i++) {
    if (self->tabs[i].terminal == terminal) {
      gtk_button_set_label(GTK_BUTTON(self->tabs[i].tab_button), title);
      gtk_widget_set_tooltip_text(self->tabs[i].tab_button, title);
      return;
    }
  }
}

/* ---- Tab management ---- */

static void sync_tab_close_visibility(SbWindow *self) {
  gboolean show = self->tab_count > 1;
  for (int i = 0; i < self->tab_count; i++) {
    if (self->tabs[i].tab_close_btn && GTK_IS_WIDGET(self->tabs[i].tab_close_btn))
      gtk_widget_set_visible(self->tabs[i].tab_close_btn, show);
  }
}

static void on_tab_close_clicked(GtkButton *btn, gpointer userdata) {
  SbWindow *self = userdata;
  for (int i = 0; i < self->tab_count; i++) {
    if (self->tabs[i].tab_close_btn == GTK_WIDGET(btn)) {
      adw_tab_view_close_page(self->tab_view, self->tabs[i].page);
      return;
    }
  }
}

static void on_tab_button_toggled(GtkToggleButton *btn, gpointer userdata);
static gboolean focus_first_tab(gpointer data);

static void on_tab_switch(AdwTabView *view, GParamSpec *pspec,
                          gpointer userdata) {
  SbWindow *self = userdata;
  (void)view;
  (void)pspec;

  AdwTabPage *sel = adw_tab_view_get_selected_page(self->tab_view);
  for (int i = 0; i < self->tab_count; i++) {
    if (!self->tabs[i].tab_button || !GTK_IS_TOGGLE_BUTTON(self->tabs[i].tab_button))
      continue;
    gboolean active = (self->tabs[i].page == sel);
    g_signal_handlers_block_by_func(self->tabs[i].tab_button,
      G_CALLBACK(on_tab_button_toggled), self);
    gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(self->tabs[i].tab_button), active);
    g_signal_handlers_unblock_by_func(self->tabs[i].tab_button,
      G_CALLBACK(on_tab_button_toggled), self);
    if (self->tabs[i].tab_row) {
      if (active)
        gtk_widget_add_css_class(self->tabs[i].tab_row, "active");
      else
        gtk_widget_remove_css_class(self->tabs[i].tab_row, "active");
    }
  }

  SbTerminal *term = active_terminal(self);
  if (term) sb_toolbar_set_active_terminal(self->toolbar, term);

  /* Defer focus grab to idle so AdwTabView can finish its page transition */
  g_idle_add(focus_first_tab, self);
}

static gboolean on_close_page(AdwTabView *view, AdwTabPage *page,
                              gpointer userdata) {
  SbWindow *self = userdata;

  if (self->tab_count <= 1)
    return GDK_EVENT_STOP; /* don't close the last tab */

  int idx = tab_index_of(self, page);
  if (idx < 0) return GDK_EVENT_PROPAGATE;

  gtk_box_remove(GTK_BOX(self->tab_bar_box), self->tabs[idx].tab_row);
  sb_terminal_free(self->tabs[idx].terminal);
  if (idx < self->tab_count - 1)
    memmove(&self->tabs[idx], &self->tabs[idx + 1],
            (self->tab_count - idx - 1) * sizeof(SbTabEntry));
  self->tab_count--;

  sync_tab_close_visibility(self);

  /* Re-focus the active terminal. notify::selected-page only fires if the
   * closed tab was the selected one, so schedule it unconditionally here.
   * Use a short timeout (one frame) to let AdwTabView finish its internal
   * page-removal and selection-change before we attempt to grab focus. */
  g_timeout_add(16, focus_first_tab, self);

  (void)view;
  return GDK_EVENT_PROPAGATE; /* let AdwTabView finish closing the page */
}

static void add_tab(SbWindow *self) {
  if (self->tab_count >= self->tab_capacity) {
    self->tab_capacity = self->tab_capacity ? self->tab_capacity * 2 : 8;
    self->tabs = g_realloc(self->tabs,
                           self->tab_capacity * sizeof(SbTabEntry));
  }

  SbTerminal *term = sb_terminal_new();
  sb_terminal_set_title_callback(term, on_terminal_title, self);
  sb_terminal_apply_theme(term, sb_theme_default());
  if (self->config_keybind_count > 0)
    sb_terminal_set_keybinds(term, self->config_keybinds,
                             self->config_keybind_count);

  GtkWidget *child = sb_terminal_get_widget(term);

  int cw, ch;
  sb_terminal_get_cell_size(term, &cw, &ch);
  gtk_widget_set_size_request(child, 80 * cw + 8, 24 * ch + 8);

  gtk_widget_set_vexpand(child, TRUE);
  gtk_widget_set_hexpand(child, TRUE);

  AdwTabPage *page = adw_tab_view_append(self->tab_view, child);

  GtkWidget *tab_btn = gtk_toggle_button_new_with_label("Terminal");
  gtk_widget_set_hexpand(tab_btn, FALSE);
  gtk_widget_set_can_focus(tab_btn, FALSE);
  gtk_widget_add_css_class(tab_btn, "flat");
  gtk_widget_add_css_class(tab_btn, "sb-tab-button");
  g_signal_connect(tab_btn, "toggled", G_CALLBACK(on_tab_button_toggled), self);

  GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
  gtk_widget_set_hexpand(close_btn, FALSE);
  gtk_widget_set_can_focus(close_btn, FALSE);
  gtk_widget_set_valign(close_btn, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(close_btn, "flat");
  gtk_widget_add_css_class(close_btn, "circular");
  gtk_widget_add_css_class(close_btn, "sb-tab-close");
  gtk_widget_set_tooltip_text(close_btn, "Close Tab");
  g_signal_connect(close_btn, "clicked", G_CALLBACK(on_tab_close_clicked), self);

  GtkWidget *tab_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(tab_row, "sb-tab-row");
  gtk_box_append(GTK_BOX(tab_row), tab_btn);
  gtk_box_append(GTK_BOX(tab_row), close_btn);

  GtkWidget *prev = gtk_widget_get_prev_sibling(self->tab_add_btn);
  if (prev)
    gtk_box_insert_child_after(GTK_BOX(self->tab_bar_box), tab_row, prev);
  else
    gtk_box_prepend(GTK_BOX(self->tab_bar_box), tab_row);

  SbTabEntry *ent = &self->tabs[self->tab_count++];
  ent->terminal = term;
  ent->page = page;
  ent->tab_button = tab_btn;
  ent->tab_close_btn = close_btn;
  ent->tab_row = tab_row;

  adw_tab_view_set_selected_page(self->tab_view, page);
  sb_toolbar_set_active_terminal(self->toolbar, term);
  gtk_widget_grab_focus(child);

  sync_tab_close_visibility(self);
}

/* ---- Shortcut: run toolbar command by index ---- */

static void run_toolbar_shortcut(SbWindow *self, int index) {
  if (index < 0 || index >= self->shortcut_count) return;
  if (!self->shortcut_cmds[index]) return;
  SbTerminal *term = active_terminal(self);
  if (term)
    sb_terminal_write_str(term, self->shortcut_cmds[index]);
}

/* ---- Key handler: Ctrl+T, Alt+1..0 ---- */

static gboolean on_key_pressed(GtkEventControllerKey *controller,
                               guint keyval, guint keycode,
                               GdkModifierType state,
                               gpointer userdata) {
  SbWindow *self = userdata;
  (void)controller;
  (void)keycode;

  int has_ctrl = (state & GDK_CONTROL_MASK) != 0;
  int has_alt  = (state & GDK_ALT_MASK) != 0;
  int has_shift = (state & GDK_SHIFT_MASK) != 0;

  /* Escape → close search if visible */
  if (keyval == GDK_KEY_Escape && !has_ctrl && !has_alt) {
    SbTerminal *term = active_terminal(self);
    if (term && sb_terminal_search_is_visible(term)) {
      sb_terminal_search_hide(term);
      return GDK_EVENT_STOP;
    }
  }

  /* Ctrl+T = new tab */
  if (keyval == GDK_KEY_t && has_ctrl && !has_shift && !has_alt) {
    add_tab(self);
    return GDK_EVENT_STOP;
  }

  /* Ctrl+F = search */
  if (keyval == GDK_KEY_f && has_ctrl && !has_shift && !has_alt) {
    SbTerminal *term = active_terminal(self);
    if (term) sb_terminal_search_toggle(term);
    return GDK_EVENT_STOP;
  }

  /* Ctrl+Plus / Ctrl+= → zoom in */
  if (has_ctrl && !has_shift && !has_alt &&
      (keyval == GDK_KEY_equal || keyval == GDK_KEY_KP_Add)) {
    SbTerminal *zoom_term = active_terminal(self);
    if (zoom_term) sb_terminal_zoom_font(zoom_term, 2);
    return GDK_EVENT_STOP;
  }

  /* Ctrl+Minus → zoom out */
  if (has_ctrl && !has_shift && !has_alt &&
      (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract)) {
    SbTerminal *zoom_term = active_terminal(self);
    if (zoom_term) sb_terminal_zoom_font(zoom_term, -2);
    return GDK_EVENT_STOP;
  }

  /* Alt+1..9 → toolbar button 1..9; Alt+0 → button 10 */
  if (has_alt && !has_ctrl && !has_shift) {
    int btn_idx = -1;
    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9)
      btn_idx = (keyval - GDK_KEY_1);  /* 0-based */
    else if (keyval == GDK_KEY_0)
      btn_idx = 9;
    if (btn_idx >= 0) {
      run_toolbar_shortcut(self, btn_idx);
      return GDK_EVENT_STOP;
    }
  }

  /* If a real text-input widget has focus (dialog entry, search bar…),
   * let GTK dispatch the event normally. */
  GtkWidget *focus_widget = gtk_root_get_focus(GTK_ROOT(self));
  if (focus_widget &&
      (GTK_IS_EDITABLE(focus_widget) || GTK_IS_TEXT_VIEW(focus_widget)))
    return GDK_EVENT_PROPAGATE;

  /* Always forward to the active terminal from the capture-phase handler.
   * This works even when gtk_widget_grab_focus() failed to land focus on the
   * drawing area (e.g. immediately after closing a tab), because we intercept
   * here before the event reaches any other widget. */
  SbTerminal *term = active_terminal(self);
  if (!term) return GDK_EVENT_PROPAGATE;

  return sb_terminal_handle_key(term, keyval, keycode, state);
}

/* ---- GActions for menu ---- */

static void on_tab_button_toggled(GtkToggleButton *btn, gpointer userdata) {
  SbWindow *self = userdata;
  if (!gtk_toggle_button_get_active(btn)) return;

  for (int i = 0; i < self->tab_count; i++) {
    if (self->tabs[i].tab_button == GTK_WIDGET(btn)) {
      if (self->tabs[i].page)
        adw_tab_view_set_selected_page(self->tab_view, self->tabs[i].page);
      return;
    }
  }
}

static void on_toolbar_toggle(GtkButton *button, gpointer userdata) {
  (void)button;
  SbWindow *self = userdata;
  gboolean visible = gtk_revealer_get_reveal_child(
    GTK_REVEALER(self->toolbar_revealer));
  gtk_revealer_set_reveal_child(GTK_REVEALER(self->toolbar_revealer), !visible);
  if (self->toolbar_toggle_da)
    gtk_widget_queue_draw(self->toolbar_toggle_da);
}

static void on_new_tab_clicked(GtkButton *button, gpointer userdata) {
  (void)button;
  add_tab((SbWindow *)userdata);
}

static void act_new_tab(GSimpleAction *action, GVariant *param,
                        gpointer userdata) {
  (void)action;
  (void)param;
  add_tab((SbWindow *)userdata);
}

static void on_pref_reload(gpointer data) {
  sb_window_reload_config((SbWindow *)data);
}

static void on_pref_closed(gpointer data) {
  SbWindow *self = data;
  SbTerminal *term = active_terminal(self);
  if (term) {
    GtkWidget *widget = sb_terminal_get_widget(term);
    gtk_widget_grab_focus(widget);
  }
}

static void act_preferences(GSimpleAction *action, GVariant *param,
                             gpointer userdata) {
  (void)action;
  (void)param;
  sb_preferences_dialog_show(GTK_WINDOW(userdata), userdata,
                             on_pref_reload, on_pref_closed, userdata);
}

static void act_about(GSimpleAction *action, GVariant *param,
                      gpointer userdata) {
  (void)action;
  (void)param;
  SbWindow *self = userdata;
  const char *authors[] = { "Xavier Araque <xavieraraque@gmail.com>", NULL };
  gtk_show_about_dialog(GTK_WINDOW(self),
    "program-name", "ShellBar",
    "version", "1.6.0",
    "comments",
    "A configurable command-bar terminal emulator for Linux, built on libghostty-vt",
    "website", "https://github.com/rendergraf/shellbar",
    "website-label", "github.com/rendergraf/shellbar",
    "copyright", "© 2026 Xavier Araque",
    "license-type", GTK_LICENSE_MIT_X11,
    "authors", authors,
    "logo-icon-name", "shellbar",
    NULL);
  SbTerminal *term = active_terminal(self);
  if (term) {
    GtkWidget *widget = sb_terminal_get_widget(term);
    gtk_widget_grab_focus(widget);
  }
}

static void act_quit(GSimpleAction *action, GVariant *param,
                     gpointer userdata) {
  (void)action;
  (void)param;
  gtk_window_destroy(GTK_WINDOW(userdata));
}

static const GActionEntry win_actions[] = {
  { "new-tab",      act_new_tab,      NULL, NULL, NULL },
  { "preferences",  act_preferences,  NULL, NULL, NULL },
  { "about",        act_about,        NULL, NULL, NULL },
  { "quit",         act_quit,         NULL, NULL, NULL },
};

/* ---- Config reload ---- */

static void update_shortcuts(SbWindow *self, const SbToolbarButtonDef *defs,
                             int count) {
  for (int i = 0; i < self->shortcut_count; i++) {
    g_free(self->shortcut_cmds[i]);
    self->shortcut_cmds[i] = NULL;
  }
  int n = count < MAX_SHORTCUTS ? count : MAX_SHORTCUTS;
  for (int i = 0; i < n; i++)
    self->shortcut_cmds[i] = g_strdup(defs[i].command);
  self->shortcut_count = n;
}

static void propagate_keybinds(SbWindow *self) {
  for (int i = 0; i < self->tab_count; i++)
    sb_terminal_set_keybinds(self->tabs[i].terminal,
                             self->config_keybinds,
                             self->config_keybind_count);
}

static void reload_buttons(SbWindow *self) {
  SbConfig *config = sb_config_load();

  if (config->button_count > 0) {
    SbToolbarButtonDef *defs = g_malloc_n(config->button_count,
                                          sizeof(SbToolbarButtonDef));
    for (int i = 0; i < config->button_count; i++) {
      defs[i].name      = config->buttons[i].name;
      defs[i].command   = config->buttons[i].command;
      defs[i].icon_name = config->buttons[i].icon;
    }
    sb_toolbar_set_buttons(self->toolbar, defs, config->button_count);
    update_shortcuts(self, defs, config->button_count);
    g_free(defs);
    for (int i = 0; i < config->button_count; i++) {
      config->buttons[i].name    = NULL;
      config->buttons[i].command = NULL;
      config->buttons[i].icon    = NULL;
    }
  } else {
    sb_toolbar_set_buttons(self->toolbar, NULL, 0);
    update_shortcuts(self, NULL, 0);
  }

  g_free(self->config_keybinds);
  self->config_keybind_count = config->keybind_count;
  self->config_keybinds = g_malloc(
    config->keybind_count * sizeof(SbConfigKeybind));
  memcpy(self->config_keybinds, config->keybinds,
         config->keybind_count * sizeof(SbConfigKeybind));
  propagate_keybinds(self);

  sb_config_free(config);
}

/* ---- SIGHUP ---- */

static int sighup_pipe[2] = {-1, -1};

static void sighup_handler(int sig) {
  (void)sig;
  if (sighup_pipe[1] >= 0) { (void)write(sighup_pipe[1], "", 1); }
}

static gboolean on_sighup_io(GIOChannel *ch, GIOCondition cond,
                             gpointer userdata) {
  (void)cond;
  char buf[64];
  while (read(g_io_channel_unix_get_fd(ch), buf, sizeof(buf)) > 0);
  reload_buttons((SbWindow *)userdata);
  return G_SOURCE_CONTINUE;
}

static void setup_sighup(SbWindow *self) {
  if (pipe(sighup_pipe) < 0) return;
  struct sigaction sa;
  sa.sa_handler = sighup_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGHUP, &sa, NULL);
  GIOChannel *ch = g_io_channel_unix_new(sighup_pipe[0]);
  g_io_add_watch(ch, G_IO_IN, on_sighup_io, self);
  g_io_channel_unref(ch);
}

/* ---- GObject lifecycle ---- */

static void sb_window_dispose(GObject *object) {
  SbWindow *self = SB_WINDOW(object);

  if (sighup_pipe[0] >= 0) { close(sighup_pipe[0]); sighup_pipe[0] = -1; }
  if (sighup_pipe[1] >= 0) { close(sighup_pipe[1]); sighup_pipe[1] = -1; }

  for (int i = 0; i < self->tab_count; i++)
    sb_terminal_free(self->tabs[i].terminal);
  g_free(self->tabs);
  self->tabs = NULL;
  self->tab_count = 0;

  for (int i = 0; i < self->shortcut_count; i++)
    g_free(self->shortcut_cmds[i]);
  self->shortcut_count = 0;

  g_free(self->config_keybinds);
  self->config_keybinds = NULL;
  self->config_keybind_count = 0;

  if (self->toolbar) {
    sb_toolbar_free(self->toolbar);
    self->toolbar = NULL;
  }

  G_OBJECT_CLASS(sb_window_parent_class)->dispose(object);
}

static void sb_window_class_init(SbWindowClass *klass) {
  GObjectClass *oclass = G_OBJECT_CLASS(klass);
  oclass->dispose = sb_window_dispose;
}

static void set_app_icon(GtkWindow *win) {
  GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(win));
  GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
  gtk_icon_theme_add_resource_path(theme, "/com/shellbar/icons");

  gtk_window_set_default_icon_name("shellbar");
}

static gboolean focus_first_tab(gpointer data) {
  SbWindow *self = data;
  AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
  if (page) {
    int idx = tab_index_of(self, page);
    if (idx >= 0)
      gtk_widget_grab_focus(sb_terminal_get_widget(self->tabs[idx].terminal));
  }
  return G_SOURCE_REMOVE;
}

static void
create_menu_popup(GtkMenuButton *button, gpointer user_data) {
  GMenuModel *model = G_MENU_MODEL(user_data);
  GtkWidget *popover = gtk_popover_menu_new_from_model(model);
  gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);
  gtk_menu_button_set_popover(button, popover);
}

/* ------------------------------------------------------------------ */
/* Utility bar                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
  const char *cmd;    /* executable to check / run  */
  const char *label;  /* tooltip                    */
  const char *glyph;  /* Unicode symbol for the btn */
} SbUtilEntry;

static const SbUtilEntry util_entries[] = {
  { "btop",     "btop — resource monitor",   "▤" },  /* bar chart grid  */
  { "htop",     "htop — process viewer",     "≡" },  /* triple bar      */
  { "top",      "top — process viewer",      "⬆" },  /* upward chart    */
  { "glances",  "glances — system monitor",  "◉" },  /* target circle   */
  { "neofetch", "neofetch — system info",    "" },  /* OS logo-ish     */
  { "fastfetch","fastfetch — system info",   "⚡" }, /* lightning bolt  */
  { "cmatrix",  "cmatrix — matrix rain",     "⣿" },  /* braille full    */
  { "tmux",     "tmux — multiplexer",        "⊞" },  /* window grid     */
  { "vim",      "vim — text editor",         "✎" },  /* pencil          */
  { "nvim",     "neovim — text editor",      "✦" },  /* neovim star     */
  { "lazygit",  "lazygit — git TUI",         "" },  /* branch-ish      */
  { "ranger",   "ranger — file manager",     "⌂" },  /* house/folder    */
  { "nnn",      "nnn — file manager",        "⋮" },  /* vertical dots   */
};

static void on_util_btn_clicked(GtkButton *btn, gpointer userdata) {
  SbWindow *self = userdata;
  const char *cmd = g_object_get_data(G_OBJECT(btn), "sb-util-cmd");
  if (!cmd) return;
  SbTerminal *term = active_terminal(self);
  if (!term) return;
  /* Write command + newline so it executes immediately */
  sb_terminal_write_str(term, cmd);
  sb_terminal_write(term, "\n", 1);
}

/* ------------------------------------------------------------------ */
/* iOS-style toggle switch drawn with Cairo                           */
/* ------------------------------------------------------------------ */

#ifndef G_PI
#define G_PI 3.14159265358979323846
#endif

/* Generic iOS switch — user_data is the GtkRevealer whose state drives it */
static void ios_switch_draw(cairo_t *cr, double w, double h, gboolean active) {
  double r  = h / 2.0;
  double kr = r - 2.5;
  double ky = h / 2.0;
  double kx = active ? (w - r) : r;

  /* Track */
  cairo_new_sub_path(cr);
  cairo_arc(cr, w - r, r, r, -G_PI / 2.0,  G_PI / 2.0);
  cairo_arc(cr, r,     r, r,  G_PI / 2.0,  3.0 * G_PI / 2.0);
  cairo_close_path(cr);
  if (active)
    cairo_set_source_rgb(cr, 0.239, 0.839, 0.361);
  else
    cairo_set_source_rgb(cr, 0.290, 0.290, 0.302);
  cairo_fill(cr);

  /* Knob shadow */
  cairo_arc(cr, kx, ky + 1.2, kr, 0, 2.0 * G_PI);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.28);
  cairo_fill(cr);

  /* Knob */
  cairo_arc(cr, kx, ky, kr, 0, 2.0 * G_PI);
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_fill(cr);
}

static void draw_ios_switch(GtkDrawingArea *area, cairo_t *cr,
                             int width, int height, gpointer user_data) {
  (void)area;
  GtkWidget *revealer = GTK_WIDGET(user_data);
  gboolean active = revealer &&
    gtk_revealer_get_reveal_child(GTK_REVEALER(revealer));
  ios_switch_draw(cr, (double)width, (double)height, active);
}

/* Same switch rotated 90° clockwise — DA must be portrait (w < h) */
static void draw_ios_switch_rotated(GtkDrawingArea *area, cairo_t *cr,
                                    int width, int height, gpointer user_data) {
  (void)area;
  GtkWidget *revealer = GTK_WIDGET(user_data);
  gboolean active = revealer &&
    gtk_revealer_get_reveal_child(GTK_REVEALER(revealer));
  /* Rotate canvas 90° CW: translate right edge to origin, then rotate */
  cairo_translate(cr, (double)width, 0.0);
  cairo_rotate(cr, G_PI / 2.0);
  /* After rotation the logical canvas is height wide × width tall */
  ios_switch_draw(cr, (double)height, (double)width, active);
}

static void on_util_bar_toggle(GtkButton *button, gpointer userdata) {
  SbWindow *self = userdata;
  gboolean visible = gtk_revealer_get_reveal_child(
    GTK_REVEALER(self->util_bar_revealer));
  gtk_revealer_set_reveal_child(
    GTK_REVEALER(self->util_bar_revealer), !visible);

  if (!visible)
    gtk_image_set_from_resource(GTK_IMAGE(self->util_toggle_img),
      "/com/shellbar/icons/scalable/actions/tools-on.svg");
  else
    gtk_image_set_from_resource(GTK_IMAGE(self->util_toggle_img),
      "/com/shellbar/icons/scalable/actions/tools-off.svg");
}

static GtkWidget *build_util_bar(SbWindow *self) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_top(box, 4);
  gtk_widget_set_margin_bottom(box, 4);
  gtk_widget_set_margin_start(box, 3);
  gtk_widget_set_margin_end(box, 3);
  gtk_widget_add_css_class(box, "sb-util-bar");
  self->util_bar_box = box;

  int found = 0;
  for (guint i = 0; i < G_N_ELEMENTS(util_entries); i++) {
    /* Only add button when the executable is on PATH */
    char *path = g_find_program_in_path(util_entries[i].cmd);
    if (!path) continue;
    g_free(path);

    GtkWidget *btn = gtk_button_new();
    gtk_widget_set_can_focus(btn, FALSE);
    gtk_widget_set_tooltip_text(btn, util_entries[i].label);
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_add_css_class(btn, "sb-util-btn");

    /* Unicode glyph label — styled via CSS to be compact */
    GtkWidget *glyph_lbl = gtk_label_new(util_entries[i].glyph);
    gtk_widget_add_css_class(glyph_lbl, "sb-util-glyph");
    gtk_button_set_child(GTK_BUTTON(btn), glyph_lbl);

    g_object_set_data_full(G_OBJECT(btn), "sb-util-cmd",
      g_strdup(util_entries[i].cmd), g_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_util_btn_clicked), self);
    gtk_box_append(GTK_BOX(box), btn);
    found++;
  }

  if (found == 0) {
    /* No utilities found: show a small label so the bar isn't invisible */
    GtkWidget *lbl = gtk_label_new("—");
    gtk_widget_add_css_class(lbl, "dim-label");
    gtk_box_append(GTK_BOX(box), lbl);
  }

  /* Wrap in a scrolled window so it doesn't overflow on short windows */
  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scroll), FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_widget_add_css_class(scroll, "sb-util-scroll");

  return scroll;
}


static void sb_window_init(SbWindow *self) {
  gtk_window_set_default_size(GTK_WINDOW(self), 900, 600);
  gtk_window_set_title(GTK_WINDOW(self), "ShellBar");

  set_app_icon(GTK_WINDOW(self));

  AdwStyleManager *sm = adw_style_manager_get_default();
  adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_DARK);

  /* ---- Theme (chrome CSS) ---- */
  sb_theme_apply_to_display(gtk_widget_get_display(GTK_WIDGET(self)),
                            sb_theme_default());

  /* ---- GActions (window-level) ---- */
  g_action_map_add_action_entries(G_ACTION_MAP(self),
    win_actions, G_N_ELEMENTS(win_actions), self);

  /* ---- Toolbar ---- */
  self->toolbar = sb_toolbar_new();
  reload_buttons(self);

  /* ---- Toolbar revealer (hidden by default, slides up from bottom) ---- */
  self->toolbar_revealer = gtk_revealer_new();
  gtk_revealer_set_transition_type(GTK_REVEALER(self->toolbar_revealer),
                                    GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
  gtk_revealer_set_transition_duration(GTK_REVEALER(self->toolbar_revealer), 250);
  gtk_revealer_set_reveal_child(GTK_REVEALER(self->toolbar_revealer), FALSE);
  gtk_revealer_set_child(GTK_REVEALER(self->toolbar_revealer),
                          sb_toolbar_get_widget(self->toolbar));

  /* ---- Utility bar revealer (created early so the toggle DA draw func
   * receives a valid pointer; child is set later after build_util_bar). ---- */
  self->util_bar_revealer = gtk_revealer_new();
  gtk_revealer_set_transition_type(GTK_REVEALER(self->util_bar_revealer),
                                   GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
  gtk_revealer_set_transition_duration(GTK_REVEALER(self->util_bar_revealer), 220);
  gtk_revealer_set_reveal_child(GTK_REVEALER(self->util_bar_revealer), FALSE);
  gtk_widget_set_vexpand(self->util_bar_revealer, TRUE);

  /* ---- Tab view ---- */
  self->tab_view = ADW_TAB_VIEW(adw_tab_view_new());
  g_signal_connect(self->tab_view, "notify::selected-page",
    G_CALLBACK(on_tab_switch), self);
  g_signal_connect(self->tab_view, "close-page",
    G_CALLBACK(on_close_page), self);

  /* ---- Custom tab bar (GtkBox in GtkScrolledWindow) ---- */
  self->tab_bar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_valign(self->tab_bar_box, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(self->tab_bar_box, TRUE);

  self->tab_bar_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->tab_bar_scroll),
    GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(self->tab_bar_scroll), FALSE);
  gtk_scrolled_window_set_min_content_height(
    GTK_SCROLLED_WINDOW(self->tab_bar_scroll), 32);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->tab_bar_scroll),
                                self->tab_bar_box);
  gtk_widget_set_size_request(self->tab_bar_scroll, -1, 34);
  gtk_widget_set_valign(self->tab_bar_scroll, GTK_ALIGN_CENTER);

  GtkCssProvider *tab_css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(tab_css,
    ".tab-title-wrap { min-height: 34px; }"
    ".tab-scroll { min-height: 34px; }"
    ".tab-scroll undershoot.top, .tab-scroll overshoot.top,"
    ".tab-scroll undershoot.bottom, .tab-scroll overshoot.bottom {"
    "  min-height: 0; background: none; }"
    /* Tab row: rounded container that holds title + close together */
    ".sb-tab-row {"
    "  background: alpha(@window_fg_color, 0.06);"
    "  border-radius: 6px;"
    "  padding: 0;"
    "  margin: 0 2px;"
    "}"
    ".sb-tab-row:hover { background: alpha(@window_fg_color, 0.10); }"
    /* Active row: solid uniform background spanning title + close button */
    ".sb-tab-row.active {"
    "  background: alpha(@window_fg_color, 0.18);"
    "}"
    ".sb-tab-row.active:hover {"
    "  background: alpha(@window_fg_color, 0.22);"
    "}"
    ".sb-tab-button {"
    "  border-radius: 5px 0 0 5px;"
    "  padding: 2px 8px 2px 10px;"
    "  min-height: 24px;"
    "  background: transparent;"
    "  box-shadow: none;"
    "  border: none;"
    "  outline: none;"
    "  color: alpha(@window_fg_color, 0.75);"
    "}"
    ".sb-tab-button:hover {"
    "  background: transparent;"
    "  color: @window_fg_color;"
    "}"
    /* Active tab text: bold, full opacity, no extra background (row provides it) */
    ".sb-tab-button:checked {"
    "  background: transparent;"
    "  color: @window_fg_color;"
    "  box-shadow: none;"
    "  border: none;"
    "  font-weight: bold;"
    "}"
    ".sb-tab-button:checked:hover {"
    "  background: transparent;"
    "}"
    /* Kill default focus ring / indicator stripes */
    ".sb-tab-button:focus,"
    ".sb-tab-button:focus-visible {"
    "  outline: none; box-shadow: none; border: none;"
    "}"
    ".sb-tab-button > check,"
    ".sb-tab-button > indicator { background: none; min-width: 0; min-height: 0;"
    "  padding: 0; margin: 0; border: none; }"
    ".sb-tab-close {"
    "  min-width: 18px; min-height: 18px;"
    "  padding: 0;"
    "  margin: 0 4px 0 0;"
    "  border-radius: 9999px;"
    "  -gtk-icon-size: 12px;"
    "  background: transparent;"
    "  color: alpha(@window_fg_color, 0.55);"
    "}"
    /* Hovering only the close icon: keep row's color, just brighten icon */
    ".sb-tab-close:hover {"
    "  background: transparent;"
    "  color: @window_fg_color;"
    "}"
    ".sb-tab-row.active .sb-tab-close {"
    "  color: @window_fg_color;"
    "}"
    /* ---- Utility bar ---- */
    ".sb-util-bar {"
    "  background: alpha(@window_fg_color, 0.06);"
     "  border-radius: 4px;"
    "}"
    ".sb-util-scroll {"
    "  background: transparent;"
    "  min-width: 10px;"
    "}"
    ".sb-util-scroll undershoot, .sb-util-scroll overshoot {"
    "  background: none; min-height: 0;"
    "}"
    ".sb-util-btn {"
    "  min-width:  8px;"
    "  min-height: 8px;"
    "  padding: 1px 1px;"
    "  margin: 0px 0px;"
    "  border-radius: 2px;"
    "  border: none;"
    "  background: alpha(@accent_bg_color, 0.06);"
    "  color: @window_fg_color;"
    "  font-size: 8px;"
    "}"
    ".sb-util-btn:hover {"
    "  background: alpha(@accent_bg_color, 0.40);"
    "}"
    ".sb-util-btn:active {"
    "  background: alpha(@accent_bg_color, 0.01);"
    "}"
    ".sb-util-btn:focus, .sb-util-btn:focus-visible {"
    "  outline: none; box-shadow: none;"
    "}"
    ".sb-util-glyph {"
    "  font-size: 8px;"
    "  font-family: 'Noto Sans Symbols 2', 'Symbola', 'Segoe UI Symbol', monospace;"
    "  min-width: 16px;"
    "  min-height: 16px;"
    "}"
  );
  gtk_style_context_add_provider_for_display(
    gtk_widget_get_display(self->tab_bar_scroll),
    GTK_STYLE_PROVIDER(tab_css),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(tab_css);
  gtk_widget_add_css_class(self->tab_bar_scroll, "tab-scroll");

  /* ---- Header bar (custom GtkBox; AdwHeaderBar's title slot has a
   * height=0 bug for GtkScrolledWindow children, and pack_start ignores
   * hexpand, so we lay it out manually instead). ---- */
  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(header, "toolbar");
  gtk_widget_add_css_class(header, "sb-chrome");
  gtk_widget_set_hexpand(header, TRUE);

  /* Toolbar toggle button (left side) — iOS switch */
  GtkWidget *toggle_btn = gtk_button_new();
  gtk_widget_set_tooltip_text(toggle_btn, "Toggle Button Bar");
  gtk_widget_set_can_focus(toggle_btn, FALSE);
  gtk_widget_add_css_class(toggle_btn, "flat");
  gtk_widget_add_css_class(toggle_btn, "sb-util-toggle-btn");

  self->toolbar_toggle_da = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(
    GTK_DRAWING_AREA(self->toolbar_toggle_da), 16);
  gtk_drawing_area_set_content_height(
    GTK_DRAWING_AREA(self->toolbar_toggle_da), 20);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->toolbar_toggle_da),
    draw_ios_switch_rotated, self->toolbar_revealer, NULL);
  gtk_widget_set_valign(self->toolbar_toggle_da, GTK_ALIGN_CENTER);
  gtk_button_set_child(GTK_BUTTON(toggle_btn), self->toolbar_toggle_da);

  g_signal_connect(toggle_btn, "clicked", G_CALLBACK(on_toolbar_toggle), self);
  gtk_box_append(GTK_BOX(header), toggle_btn);

  /* Tab bar fills the central horizontal space */
  gtk_widget_set_hexpand(self->tab_bar_scroll, TRUE);
  gtk_widget_set_halign(self->tab_bar_scroll, GTK_ALIGN_FILL);
  gtk_widget_set_valign(self->tab_bar_scroll, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(header), self->tab_bar_scroll);

  /* New tab button (at end of tab bar) */
  self->tab_add_btn = gtk_button_new_with_label("+");
  gtk_widget_set_tooltip_text(self->tab_add_btn, "New Tab (Ctrl+T)");
  gtk_widget_set_can_focus(self->tab_add_btn, FALSE);
  gtk_widget_set_hexpand(self->tab_add_btn, FALSE);
  g_signal_connect(self->tab_add_btn, "clicked", G_CALLBACK(on_new_tab_clicked), self);
  gtk_box_append(GTK_BOX(self->tab_bar_box), self->tab_add_btn);

  /* Menu button */
  GtkWidget *menu_btn = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn),
                                "open-menu-symbolic");
  gtk_widget_set_can_focus(menu_btn, FALSE);
  gtk_widget_add_css_class(menu_btn, "flat");
  gtk_widget_set_tooltip_text(menu_btn, "Main Menu");
  GMenu *menu = g_menu_new();
  g_menu_append(menu, "New Tab", "win.new-tab");
  g_menu_append(menu, "Preferences", "win.preferences");
  g_menu_append(menu, "About", "win.about");
  g_menu_append(menu, "Quit", "win.quit");
  g_object_ref_sink(menu);
  gtk_menu_button_set_create_popup_func(GTK_MENU_BUTTON(menu_btn),
    create_menu_popup, G_MENU_MODEL(menu), (GDestroyNotify)g_object_unref);
  gtk_box_append(GTK_BOX(header), menu_btn);

  /* Utility bar toggle button — icon-based, green=ON / gray=OFF */
  GtkWidget *util_toggle_btn = gtk_button_new();
  gtk_widget_set_tooltip_text(util_toggle_btn, "Toggle Utility Bar");
  gtk_widget_set_can_focus(util_toggle_btn, FALSE);
  gtk_widget_add_css_class(util_toggle_btn, "flat");
  gtk_widget_add_css_class(util_toggle_btn, "sb-util-toggle-btn");

  self->util_toggle_img = gtk_image_new_from_resource(
    "/com/shellbar/icons/scalable/actions/tools-off.svg");
  gtk_image_set_pixel_size(GTK_IMAGE(self->util_toggle_img), 18);
  gtk_widget_set_size_request(self->util_toggle_img, 24, 24);
  gtk_widget_set_valign(self->util_toggle_img, GTK_ALIGN_CENTER);
  gtk_button_set_child(GTK_BUTTON(util_toggle_btn), self->util_toggle_img);

  g_signal_connect(util_toggle_btn, "clicked", G_CALLBACK(on_util_bar_toggle), self);
  gtk_box_append(GTK_BOX(header), util_toggle_btn);

  /* Window controls (minimize/maximize/close) */
  GtkWidget *win_controls = gtk_window_controls_new(GTK_PACK_END);
  gtk_window_controls_set_decoration_layout(
    GTK_WINDOW_CONTROLS(win_controls), ":minimize,maximize,close");
  gtk_box_append(GTK_BOX(header), win_controls);

  /* ---- Utility bar revealer child (revealer itself was created earlier) ---- */
  GtkWidget *util_bar_widget = build_util_bar(self);
  gtk_revealer_set_child(GTK_REVEALER(self->util_bar_revealer), util_bar_widget);

  /* Horizontal box: terminal area (expands) + util bar (fixed width) */
  GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(GTK_WIDGET(self->tab_view), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(self->tab_view), TRUE);
  gtk_box_append(GTK_BOX(content_box), GTK_WIDGET(self->tab_view));
  gtk_box_append(GTK_BOX(content_box), self->util_bar_revealer);

  /* ---- Assemble toolbar view (bars at bottom) ---- */
  self->toolbar_view = adw_toolbar_view_new();
  adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(self->toolbar_view),
                                  self->toolbar_revealer);
  adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(self->toolbar_view), header);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(self->toolbar_view), content_box);

  adw_application_window_set_content(ADW_APPLICATION_WINDOW(self),
                                     self->toolbar_view);

  /* ---- Key controller on the root window (capture phase) ---- */
  /* Attaching to GTK_WIDGET(self) — the AdwApplicationWindow — guarantees
   * we see key events regardless of which internal widget currently holds
   * focus (AdwTabView internals, the window itself after a tab close, etc.) */
  GtkEventController *k = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(k, GTK_PHASE_CAPTURE);
  g_signal_connect(k, "key-pressed", G_CALLBACK(on_key_pressed), self);
  gtk_widget_add_controller(GTK_WIDGET(self), k);

  /* ---- First tab ---- */
  add_tab(self);

  /* ---- Async focus ---- */
  g_idle_add(focus_first_tab, self);

  /* ---- Signal reload ---- */
  setup_sighup(self);
}

void sb_window_reload_config(SbWindow *self) {
  reload_buttons(self);
}

SbWindow *sb_window_new(AdwApplication *app) {
  return g_object_new(SB_TYPE_WINDOW, "application", app, NULL);
}
