/*
 * ShellBar v1.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#include "sb_window.h"
#include "sb_toolbar.h"
#include "sb_config.h"
#include "sb_preferences_dialog.h"

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
} SbTabEntry;

/* ------------------------------------------------------------------ */
/* Window struct                                                       */
/* ------------------------------------------------------------------ */

#define MAX_SHORTCUTS 10

struct _SbWindow {
  AdwApplicationWindow parent;
  SbToolbar *toolbar;
  AdwTabView *tab_view;
  AdwTabBar *tab_bar;
  GtkWidget *toolbar_view;
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

/* Toggle close buttons: hide when only 1 tab */
static void toggle_buttons_recursive(GtkWidget *w, gboolean show) {
  if (GTK_IS_BUTTON(w)) {
    gtk_widget_set_visible(w, show);
    return;
  }
  GtkWidget *c = gtk_widget_get_first_child(w);
  while (c) {
    toggle_buttons_recursive(c, show);
    c = gtk_widget_get_next_sibling(c);
  }
}

static void update_tab_close_visibility(SbWindow *self) {
  toggle_buttons_recursive(GTK_WIDGET(self->tab_bar), self->tab_count > 1);
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
      adw_tab_page_set_title(self->tabs[i].page, title);
      adw_tab_page_set_tooltip(self->tabs[i].page, title);
      return;
    }
  }
}

/* ---- Tab management ---- */

static void on_tab_switch(AdwTabView *view, GParamSpec *pspec,
                          gpointer userdata) {
  SbWindow *self = userdata;
  (void)view;
  (void)pspec;
  SbTerminal *term = active_terminal(self);
  if (term) sb_toolbar_set_active_terminal(self->toolbar, term);
}

static gboolean on_close_page(AdwTabView *view, AdwTabPage *page,
                              gpointer userdata) {
  SbWindow *self = userdata;

  if (self->tab_count <= 1)
    return GDK_EVENT_STOP; /* don't close the last tab */

  int idx = tab_index_of(self, page);
  if (idx < 0) return GDK_EVENT_PROPAGATE;

  sb_terminal_free(self->tabs[idx].terminal);
  if (idx < self->tab_count - 1)
    memmove(&self->tabs[idx], &self->tabs[idx + 1],
            (self->tab_count - idx - 1) * sizeof(SbTabEntry));
  self->tab_count--;

  update_tab_close_visibility(self);

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
  if (self->config_keybind_count > 0)
    sb_terminal_set_keybinds(term, self->config_keybinds,
                             self->config_keybind_count);

  GtkWidget *child = sb_terminal_get_widget(term);
  gtk_widget_set_vexpand(child, TRUE);
  gtk_widget_set_hexpand(child, TRUE);

  AdwTabPage *page = adw_tab_view_append(self->tab_view, child);
  adw_tab_page_set_title(page, "Terminal");

  SbTabEntry *ent = &self->tabs[self->tab_count++];
  ent->terminal = term;
  ent->page = page;

  adw_tab_view_set_selected_page(self->tab_view, page);
  sb_toolbar_set_active_terminal(self->toolbar, term);
  gtk_widget_grab_focus(child);
  update_tab_close_visibility(self);
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

  /* Ctrl+T = new tab */
  if (keyval == GDK_KEY_t && has_ctrl && !has_shift && !has_alt) {
    add_tab(self);
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

  SbTerminal *term = active_terminal(self);
  if (!term) return GDK_EVENT_PROPAGATE;
  return sb_terminal_handle_key(term, keyval, keycode, state);
}

/* ---- GActions for menu ---- */

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

static void act_preferences(GSimpleAction *action, GVariant *param,
                             gpointer userdata) {
  (void)action;
  (void)param;
  sb_preferences_dialog_show(GTK_WINDOW(userdata), userdata,
                             on_pref_reload);
}

static void act_about(GSimpleAction *action, GVariant *param,
                      gpointer userdata) {
  (void)action;
  (void)param;
  SbWindow *self = userdata;
  const char *authors[] = { "Xavier Araque <xavieraraque@gmail.com>", NULL };
  gtk_show_about_dialog(GTK_WINDOW(self),
    "program-name", "ShellBar",
    "version", "1.0",
    "comments",
    "A command-bar terminal emulator built on libghostty-vt",
    "website", "https://github.com/rendergraf/shellbar",
    "website-label", "github.com/rendergraf/shellbar",
    "copyright", "© 2026 Xavier Araque",
    "license-type", GTK_LICENSE_MIT_X11,
    "authors", authors,
    NULL);
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
  if (sighup_pipe[1] >= 0) write(sighup_pipe[1], "", 1);
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

static void sb_window_init(SbWindow *self) {
  gtk_window_set_default_size(GTK_WINDOW(self), 900, 600);
  gtk_window_set_title(GTK_WINDOW(self), "ShellBar");

  set_app_icon(GTK_WINDOW(self));

  AdwStyleManager *sm = adw_style_manager_get_default();
  adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_DARK);

  /* ---- GActions (window-level) ---- */
  g_action_map_add_action_entries(G_ACTION_MAP(self),
    win_actions, G_N_ELEMENTS(win_actions), self);

  /* ---- Toolbar ---- */
  self->toolbar = sb_toolbar_new();
  reload_buttons(self);

  /* ---- Tab view ---- */
  self->tab_view = ADW_TAB_VIEW(adw_tab_view_new());
  g_signal_connect(self->tab_view, "notify::selected-page",
    G_CALLBACK(on_tab_switch), self);
  g_signal_connect(self->tab_view, "close-page",
    G_CALLBACK(on_close_page), self);

  /* ---- Tab bar ---- */
  self->tab_bar = ADW_TAB_BAR(adw_tab_bar_new());
  adw_tab_bar_set_view(self->tab_bar, self->tab_view);
  adw_tab_bar_set_autohide(self->tab_bar, FALSE);

  /* ---- Header bar ---- */
  GtkWidget *header = adw_header_bar_new();
  adw_header_bar_set_show_title(ADW_HEADER_BAR(header), FALSE);
  adw_header_bar_set_decoration_layout(ADW_HEADER_BAR(header),
    ":minimize,maximize,close");

  /* New tab button */
  GtkWidget *new_btn = gtk_button_new_with_label("+");
  gtk_widget_set_tooltip_text(new_btn, "New Tab (Ctrl+T)");
  g_signal_connect(new_btn, "clicked", G_CALLBACK(on_new_tab_clicked), self);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), new_btn);

  /* Menu button */
  GtkWidget *menu_btn = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn),
                                "open-menu-symbolic");
  GMenu *menu = g_menu_new();
  g_menu_append(menu, "New Tab", "win.new-tab");
  g_menu_append(menu, "Preferences", "win.preferences");
  g_menu_append(menu, "About", "win.about");
  g_menu_append(menu, "Quit", "win.quit");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn),
                                 G_MENU_MODEL(menu));
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_btn);

  /* ---- Assemble toolbar view ---- */
  self->toolbar_view = adw_toolbar_view_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(self->toolbar_view), header);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(self->toolbar_view),
                               GTK_WIDGET(self->tab_bar));
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(self->toolbar_view),
                               sb_toolbar_get_widget(self->toolbar));
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(self->toolbar_view),
                               GTK_WIDGET(self->tab_view));

  adw_application_window_set_content(ADW_APPLICATION_WINDOW(self),
                                     self->toolbar_view);

  /* ---- Key controller on the toolbar view ---- */
  GtkEventController *k = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(k, GTK_PHASE_CAPTURE);
  g_signal_connect(k, "key-pressed", G_CALLBACK(on_key_pressed), self);
  gtk_widget_add_controller(self->toolbar_view, k);

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
