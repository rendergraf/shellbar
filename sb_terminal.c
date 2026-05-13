/*
 * ShellBar v1.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#include "sb_terminal.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include <ghostty/vt.h>

/* ------------------------------------------------------------------ */
/* Private struct                                                     */
/* ------------------------------------------------------------------ */

struct _SbTerminal {
  GtkWidget *widget;
  GtkDrawingArea *drawing_area;

  int pty_fd;
  pid_t child_pid;
  guint pty_source;
  guint cursor_timer;

  GhosttyTerminal terminal;
  GhosttyRenderState render_state;
  GhosttyRenderStateRowIterator row_iter;
  GhosttyRenderStateRowCells cells;

  GhosttyKeyEncoder key_encoder;
  GhosttyKeyEvent key_event;

  GhosttyMouseEncoder mouse_encoder;
  GhosttyMouseEvent mouse_event;

  PangoFontDescription *font_desc;
  int font_size;
  int cell_width;
  int cell_height;
  int padding;

  uint16_t cols;
  uint16_t rows;

  int last_alloc_w;
  int last_alloc_h;

  bool cursor_visible;

  SbTerminalTitleCb title_cb;
  void *title_cb_data;
};

/* ------------------------------------------------------------------ */
/* PTY helpers                                                         */
/* ------------------------------------------------------------------ */

static int pty_spawn(pid_t *child_out, uint16_t cols, uint16_t rows,
                     int cell_width, int cell_height) {
  int pty_fd;
  struct winsize ws = {
    .ws_row = rows,
    .ws_col = cols,
    .ws_xpixel = (unsigned short)(cols * cell_width),
    .ws_ypixel = (unsigned short)(rows * cell_height),
  };

  pid_t child = forkpty(&pty_fd, NULL, NULL, &ws);
  if (child < 0) return -1;

  if (child == 0) {
    const char *shell = getenv("SHELL");
    if (!shell || shell[0] == '\0') {
      struct passwd *pw = getpwuid(getuid());
      if (pw && pw->pw_shell && pw->pw_shell[0] != '\0')
        shell = pw->pw_shell;
      else
        shell = "/bin/sh";
    }
    const char *shell_name = strrchr(shell, '/');
    shell_name = shell_name ? shell_name + 1 : shell;
    setenv("TERM", "xterm-256color", 1);
    execl(shell, shell_name, NULL);
    _exit(127);
  }

  int flags = fcntl(pty_fd, F_GETFL);
  if (flags >= 0)
    fcntl(pty_fd, F_SETFL, flags | O_NONBLOCK);

  *child_out = child;
  return pty_fd;
}

static void pty_write_raw(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t n = write(fd, buf, len);
    if (n > 0) {
      buf += n;
      len -= (size_t)n;
    } else if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Effect callbacks                                                    */
/* ------------------------------------------------------------------ */

static void effect_write_pty(GhosttyTerminal terminal, void *userdata,
                             const uint8_t *data, size_t len) {
  (void)terminal;
  int pty_fd = *(int *)userdata;
  pty_write_raw(pty_fd, (const char *)data, len);
}

static void effect_title_changed(GhosttyTerminal terminal, void *userdata) {
  SbTerminal *self = userdata;
  GhosttyString title = {0};
  if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_TITLE, &title)
      != GHOSTTY_SUCCESS || title.len == 0)
    return;
  char buf[256];
  size_t len = title.len < sizeof(buf) - 1 ? title.len : sizeof(buf) - 1;
  memcpy(buf, title.ptr, len);
  buf[len] = '\0';
  if (self->title_cb)
    self->title_cb(self, buf, self->title_cb_data);
}

static bool effect_size(GhosttyTerminal terminal, void *userdata,
                        GhosttySizeReportSize *out_size) {
  (void)terminal;
  SbTerminal *self = userdata;
  out_size->rows = self->rows;
  out_size->columns = self->cols;
  out_size->cell_width = (uint32_t)self->cell_width;
  out_size->cell_height = (uint32_t)self->cell_height;
  return true;
}

static bool effect_device_attributes(GhosttyTerminal terminal, void *userdata,
                                     GhosttyDeviceAttributes *out_attrs) {
  (void)terminal;
  (void)userdata;
  out_attrs->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
  out_attrs->primary.features[0] = GHOSTTY_DA_FEATURE_COLUMNS_132;
  out_attrs->primary.features[1] = GHOSTTY_DA_FEATURE_SELECTIVE_ERASE;
  out_attrs->primary.features[2] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
  out_attrs->primary.num_features = 3;
  out_attrs->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
  out_attrs->secondary.firmware_version = 1;
  out_attrs->secondary.rom_cartridge = 0;
  out_attrs->tertiary.unit_id = 0;
  return true;
}

static GhosttyString effect_xtversion(GhosttyTerminal terminal, void *userdata) {
  (void)terminal;
  (void)userdata;
  return (GhosttyString){ .ptr = (const uint8_t *)"shellbar 0.1.0", .len = 16 };
}

static bool effect_color_scheme(GhosttyTerminal terminal, void *userdata,
                                GhosttyColorScheme *out_scheme) {
  (void)terminal;
  (void)userdata;
  (void)out_scheme;
  return false;
}

/* ------------------------------------------------------------------ */
/* PTY readable callback (GLib IO watch)                               */
/* ------------------------------------------------------------------ */

static gboolean on_pty_readable(GIOChannel *channel,
                                GIOCondition condition,
                                gpointer user_data) {
  SbTerminal *self = user_data;
  if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
    return G_SOURCE_REMOVE;
  }

  int fd = g_io_channel_unix_get_fd(channel);
  uint8_t buf[4096];
  bool dirty = false;
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0) {
      ghostty_terminal_vt_write(self->terminal, buf, (size_t)n);
      dirty = true;
    } else if (n == 0) {
      return G_SOURCE_REMOVE;
    } else {
      if (errno == EAGAIN) break;
      if (errno == EINTR) continue;
      if (errno == EIO) return G_SOURCE_REMOVE;
      break;
    }
  }

  if (dirty) {
    ghostty_render_state_update(self->render_state, self->terminal);
    gtk_widget_queue_draw(self->widget);
  }

  return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Key mapping GDK → Ghostty                                           */
/* ------------------------------------------------------------------ */

static GhosttyKey gdk_keyval_to_ghostty(guint keyval) {
  if (keyval >= GDK_KEY_a && keyval <= GDK_KEY_z)
    return GHOSTTY_KEY_A + (keyval - GDK_KEY_a);
  if (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z)
    return GHOSTTY_KEY_A + (keyval - GDK_KEY_A);
  if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9)
    return GHOSTTY_KEY_DIGIT_0 + (keyval - GDK_KEY_0);
  if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F12)
    return GHOSTTY_KEY_F1 + (keyval - GDK_KEY_F1);
  if (keyval >= GDK_KEY_KP_0 && keyval <= GDK_KEY_KP_9)
    return GHOSTTY_KEY_NUMPAD_0 + (keyval - GDK_KEY_KP_0);

  switch (keyval) {
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: return GHOSTTY_KEY_ENTER;
    case GDK_KEY_BackSpace: return GHOSTTY_KEY_BACKSPACE;
    case GDK_KEY_Tab: case GDK_KEY_KP_Tab: return GHOSTTY_KEY_TAB;
    case GDK_KEY_Escape: return GHOSTTY_KEY_ESCAPE;
    case GDK_KEY_Delete: case GDK_KEY_KP_Delete: return GHOSTTY_KEY_DELETE;
    case GDK_KEY_Home: case GDK_KEY_KP_Home: return GHOSTTY_KEY_HOME;
    case GDK_KEY_End: case GDK_KEY_KP_End: return GHOSTTY_KEY_END;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: return GHOSTTY_KEY_PAGE_UP;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: return GHOSTTY_KEY_PAGE_DOWN;
    case GDK_KEY_Insert: case GDK_KEY_KP_Insert: return GHOSTTY_KEY_INSERT;
    case GDK_KEY_Up: return GHOSTTY_KEY_ARROW_UP;
    case GDK_KEY_Down: return GHOSTTY_KEY_ARROW_DOWN;
    case GDK_KEY_Left: return GHOSTTY_KEY_ARROW_LEFT;
    case GDK_KEY_Right: return GHOSTTY_KEY_ARROW_RIGHT;
    case GDK_KEY_space: return GHOSTTY_KEY_SPACE;
    case GDK_KEY_minus: case GDK_KEY_KP_Subtract: return GHOSTTY_KEY_MINUS;
    case GDK_KEY_equal: return GHOSTTY_KEY_EQUAL;
    case GDK_KEY_bracketleft: return GHOSTTY_KEY_BRACKET_LEFT;
    case GDK_KEY_bracketright: return GHOSTTY_KEY_BRACKET_RIGHT;
    case GDK_KEY_backslash: return GHOSTTY_KEY_BACKSLASH;
    case GDK_KEY_semicolon: return GHOSTTY_KEY_SEMICOLON;
    case GDK_KEY_apostrophe: return GHOSTTY_KEY_QUOTE;
    case GDK_KEY_comma: return GHOSTTY_KEY_COMMA;
    case GDK_KEY_period: case GDK_KEY_KP_Decimal: return GHOSTTY_KEY_PERIOD;
    case GDK_KEY_slash: return GHOSTTY_KEY_SLASH;
    case GDK_KEY_grave: return GHOSTTY_KEY_BACKQUOTE;
    default: return GHOSTTY_KEY_UNIDENTIFIED;
  }
}

static uint32_t gdk_keyval_unshifted_codepoint(guint keyval) {
  if (keyval >= GDK_KEY_a && keyval <= GDK_KEY_z)
    return 'a' + (keyval - GDK_KEY_a);
  if (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z)
    return 'a' + (keyval - GDK_KEY_A);
  if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9)
    return '0' + (keyval - GDK_KEY_0);
  switch (keyval) {
    case GDK_KEY_space: return ' ';
    case GDK_KEY_minus: return '-';
    case GDK_KEY_equal: return '=';
    case GDK_KEY_bracketleft: return '[';
    case GDK_KEY_bracketright: return ']';
    case GDK_KEY_backslash: return '\\';
    case GDK_KEY_semicolon: return ';';
    case GDK_KEY_apostrophe: return '\'';
    case GDK_KEY_comma: return ',';
    case GDK_KEY_period: return '.';
    case GDK_KEY_slash: return '/';
    case GDK_KEY_grave: return '`';
    default: return 0;
  }
}

/* ------------------------------------------------------------------ */
/* Keyboard event handlers                                            */
/* ------------------------------------------------------------------ */

static gboolean on_terminal_key(GtkEventControllerKey *controller,
                                guint keyval, guint keycode,
                                GdkModifierType state,
                                gpointer user_data) {
  (void)controller;
  return sb_terminal_handle_key((SbTerminal *)user_data,
                                keyval, keycode, state);
}

gboolean sb_terminal_handle_key(SbTerminal *self, guint keyval,
                                guint keycode, GdkModifierType state) {
  (void)keycode;

  static char sb_utf8[8] = {0};
  bool have_utf8 = false;
  gunichar uc = gdk_keyval_to_unicode(keyval);
  if (uc >= 0x20 && uc != 0x7F && uc <= 0x10FFFF) {
    memset(sb_utf8, 0, sizeof(sb_utf8));
    gint ulen = g_unichar_to_utf8(uc, sb_utf8);
    have_utf8 = ulen > 0;
  }

  GhosttyKey gkey = gdk_keyval_to_ghostty(keyval);
  if (gkey == GHOSTTY_KEY_UNIDENTIFIED) {
    if (have_utf8) {
      pty_write_raw(self->pty_fd, sb_utf8, strlen(sb_utf8));
      return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
  }

  GhosttyMods mods = 0;
  if (state & GDK_SHIFT_MASK) mods |= GHOSTTY_MODS_SHIFT;
  if (state & GDK_CONTROL_MASK) mods |= GHOSTTY_MODS_CTRL;
  if (state & GDK_ALT_MASK) mods |= GHOSTTY_MODS_ALT;
  if (state & GDK_SUPER_MASK) mods |= GHOSTTY_MODS_SUPER;

  ghostty_key_encoder_setopt_from_terminal(self->key_encoder, self->terminal);

  ghostty_key_event_set_key(self->key_event, gkey);
  ghostty_key_event_set_action(self->key_event, GHOSTTY_KEY_ACTION_PRESS);
  ghostty_key_event_set_mods(self->key_event, mods);

  uint32_t ucp = gdk_keyval_unshifted_codepoint(keyval);
  ghostty_key_event_set_unshifted_codepoint(self->key_event, ucp);

  GhosttyMods consumed = 0;
  if (ucp != 0 && (mods & GHOSTTY_MODS_SHIFT))
    consumed |= GHOSTTY_MODS_SHIFT;
  ghostty_key_event_set_consumed_mods(self->key_event, consumed);

  if (have_utf8)
    ghostty_key_event_set_utf8(self->key_event, sb_utf8, strlen(sb_utf8));
  else
    ghostty_key_event_set_utf8(self->key_event, NULL, 0);

  char buf[128];
  size_t written = 0;
  GhosttyResult res = ghostty_key_encoder_encode(
    self->key_encoder, self->key_event, buf, sizeof(buf), &written);
  if (res != GHOSTTY_SUCCESS || written == 0) {
    if (have_utf8) {
      pty_write_raw(self->pty_fd, sb_utf8, strlen(sb_utf8));
      return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
  }

  pty_write_raw(self->pty_fd, buf, written);
  return GDK_EVENT_STOP;
}

/* ------------------------------------------------------------------ */
/* Scroll event handler                                                 */
/* ------------------------------------------------------------------ */

static gboolean on_scroll(GtkEventControllerScroll *controller,
                          gdouble dx, gdouble dy,
                          gpointer user_data) {
  (void)controller;
  (void)dx;
  SbTerminal *self = user_data;

  bool mouse_tracking = false;
  ghostty_terminal_get(self->terminal,
    GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse_tracking);

  if (mouse_tracking) {
    GhosttyMouseButton btn = (dy < 0)
      ? GHOSTTY_MOUSE_BUTTON_FOUR
      : GHOSTTY_MOUSE_BUTTON_FIVE;
    ghostty_mouse_event_set_button(self->mouse_event, btn);
    ghostty_mouse_event_set_action(self->mouse_event, GHOSTTY_MOUSE_ACTION_PRESS);
    ghostty_mouse_encoder_setopt_from_terminal(self->mouse_encoder, self->terminal);
    ghostty_mouse_event_set_position(self->mouse_event,
      (GhosttyMousePosition){ .x = 0, .y = 0 });

    char buf[128];
    size_t written = 0;
    ghostty_mouse_encoder_encode(self->mouse_encoder, self->mouse_event,
                                 buf, sizeof(buf), &written);
    if (written > 0) pty_write_raw(self->pty_fd, buf, written);

    ghostty_mouse_event_set_action(self->mouse_event, GHOSTTY_MOUSE_ACTION_RELEASE);
    ghostty_mouse_encoder_encode(self->mouse_encoder, self->mouse_event,
                                 buf, sizeof(buf), &written);
    if (written > 0) pty_write_raw(self->pty_fd, buf, written);
  } else {
    int delta = (dy > 0) ? -3 : 3;
    GhosttyTerminalScrollViewport sv = {
      .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
      .value = { .delta = delta },
    };
    ghostty_terminal_scroll_viewport(self->terminal, sv);
    ghostty_render_state_update(self->render_state, self->terminal);
    gtk_widget_queue_draw(self->widget);
  }

  return GDK_EVENT_STOP;
}

/* ------------------------------------------------------------------ */
/* Resize handler                                                      */
/* ------------------------------------------------------------------ */

static void recalc_geometry(SbTerminal *self, int alloc_w, int alloc_h) {
  if (alloc_w == self->last_alloc_w && alloc_h == self->last_alloc_h)
    return;
  self->last_alloc_w = alloc_w;
  self->last_alloc_h = alloc_h;

  int inner_w = alloc_w - self->padding * 2;
  int inner_h = alloc_h - self->padding * 2;
  if (inner_w < 10) inner_w = 10;
  if (inner_h < 10) inner_h = 10;

  uint16_t new_cols = (uint16_t)(inner_w / self->cell_width);
  uint16_t new_rows = (uint16_t)(inner_h / self->cell_height);
  if (new_cols < 2) new_cols = 2;
  if (new_rows < 2) new_rows = 2;

  if (new_cols != self->cols || new_rows != self->rows) {
    self->cols = new_cols;
    self->rows = new_rows;
    ghostty_terminal_resize(self->terminal, self->cols, self->rows,
                            (uint32_t)self->cell_width,
                            (uint32_t)self->cell_height);

    struct winsize ws = {
      .ws_row = self->rows,
      .ws_col = self->cols,
      .ws_xpixel = (unsigned short)(self->cols * self->cell_width),
      .ws_ypixel = (unsigned short)(self->rows * self->cell_height),
    };
    ioctl(self->pty_fd, TIOCSWINSZ, &ws);
  }
}

/* ------------------------------------------------------------------ */
/* Render function (Cairo draw callback)                                */
/* ------------------------------------------------------------------ */

static void render_terminal(GtkDrawingArea *area, cairo_t *cr,
                            int width, int height, gpointer user_data) {
  SbTerminal *self = user_data;
  (void)area;

  recalc_geometry(self, width, height);

  /* Colors */
  GhosttyRenderStateColors colors =
    GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
  ghostty_render_state_colors_get(self->render_state, &colors);

  GdkRGBA bg_color;
  bg_color.red   = colors.background.r / 255.0;
  bg_color.green = colors.background.g / 255.0;
  bg_color.blue  = colors.background.b / 255.0;
  bg_color.alpha = 1.0;

  /* Clear background */
  cairo_set_source_rgba(cr, bg_color.red, bg_color.green, bg_color.blue, 1.0);
  cairo_paint(cr);

  /* Populate row iterator */
  if (ghostty_render_state_get(self->render_state,
        GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &self->row_iter)
      != GHOSTTY_SUCCESS)
    return;

  /* Font setup */
  PangoLayout *layout = gtk_widget_create_pango_layout(self->widget, NULL);
  pango_layout_set_font_description(layout, self->font_desc);

  int y = self->padding;
  while (ghostty_render_state_row_iterator_next(self->row_iter)) {
    if (ghostty_render_state_row_get(self->row_iter,
          GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &self->cells)
        != GHOSTTY_SUCCESS)
      continue;

    int x = self->padding;
    while (ghostty_render_state_row_cells_next(self->cells)) {
      uint32_t grapheme_len = 0;
      ghostty_render_state_row_cells_get(self->cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);

      /* Background color */
      GhosttyColorRgb bg = {0};
      bool has_bg = ghostty_render_state_row_cells_get(self->cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg) == GHOSTTY_SUCCESS;

      if (grapheme_len == 0) {
        if (has_bg) {
          cairo_rectangle(cr, x, y, self->cell_width, self->cell_height);
          cairo_set_source_rgb(cr, bg.r / 255.0, bg.g / 255.0, bg.b / 255.0);
          cairo_fill(cr);
        }
        x += self->cell_width;
        continue;
      }

      uint32_t codepoints[16];
      uint32_t len = grapheme_len < 16 ? grapheme_len : 16;
      ghostty_render_state_row_cells_get(self->cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, codepoints);

      char text[64];
      int pos = 0;
      for (uint32_t i = 0; i < len && pos < 60; i++) {
        uint32_t cp = codepoints[i];
        if (cp < 0x80) {
          text[pos++] = (char)cp;
        } else if (cp < 0x800) {
          text[pos++] = (char)(0xC0 | (cp >> 6));
          text[pos++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
          text[pos++] = (char)(0xE0 | (cp >> 12));
          text[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
          text[pos++] = (char)(0x80 | (cp & 0x3F));
        } else {
          text[pos++] = (char)(0xF0 | (cp >> 18));
          text[pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
          text[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
          text[pos++] = (char)(0x80 | (cp & 0x3F));
        }
      }
      text[pos] = '\0';

      GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
      ghostty_render_state_row_cells_get(self->cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

      GhosttyColorRgb fg = colors.foreground;
      ghostty_render_state_row_cells_get(self->cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);

      if (style.inverse) {
        GhosttyColorRgb tmp = fg;
        fg = bg;
        bg = tmp;
        has_bg = true;
      }

      /* Draw background */
      if (has_bg) {
        cairo_rectangle(cr, x, y, self->cell_width, self->cell_height);
        cairo_set_source_rgb(cr, bg.r / 255.0, bg.g / 255.0, bg.b / 255.0);
        cairo_fill(cr);
      }

      /* Draw text */
      int italic_offset = style.italic ? (self->font_size / 6) : 0;
      cairo_set_source_rgb(cr, fg.r / 255.0, fg.g / 255.0, fg.b / 255.0);
      pango_layout_set_text(layout, text, pos);
      cairo_move_to(cr, x + italic_offset, y);
      pango_cairo_show_layout(cr, layout);

      if (style.bold) {
        cairo_move_to(cr, x + italic_offset + 1, y);
        pango_cairo_show_layout(cr, layout);
      }

      x += self->cell_width;
    }

    /* Clear per-row dirty */
    bool clean = false;
    ghostty_render_state_row_set(self->row_iter,
      GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &clean);
    y += self->cell_height;
  }

  /* Draw cursor */
  bool cursor_in_viewport = false;
  ghostty_render_state_get(self->render_state,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursor_in_viewport);
  bool cursor_visible = false;
  ghostty_render_state_get(self->render_state,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursor_visible);

  if (cursor_visible && cursor_in_viewport && self->cursor_visible) {
    uint16_t cx = 0, cy = 0;
    ghostty_render_state_get(self->render_state,
      GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
    ghostty_render_state_get(self->render_state,
      GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);

    GhosttyColorRgb cur = colors.cursor;
    if (!colors.cursor_has_value)
      cur = colors.foreground;

    int cur_x = self->padding + cx * self->cell_width;
    int cur_y = self->padding + cy * self->cell_height;
    cairo_rectangle(cr, cur_x, cur_y, self->cell_width, self->cell_height);
    cairo_set_source_rgba(cr, cur.r / 255.0, cur.g / 255.0, cur.b / 255.0, 0.5);
    cairo_fill(cr);
  }

  /* Draw scrollbar */
  GhosttyTerminalScrollbar scrollbar = {0};
  if (ghostty_terminal_get(self->terminal,
        GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) == GHOSTTY_SUCCESS
      && scrollbar.total > scrollbar.len) {
    const int bar_w = 6;
    const int bar_margin = 2;
    int bar_x = width - bar_w - bar_margin;

    double visible_frac = (double)scrollbar.len / (double)scrollbar.total;
    int thumb_h = (int)(height * visible_frac);
    if (thumb_h < 10) thumb_h = 10;

    double scroll_frac = (scrollbar.total > scrollbar.len)
      ? (double)scrollbar.offset / (double)(scrollbar.total - scrollbar.len)
      : 1.0;
    int thumb_y = (int)(scroll_frac * (double)(height - thumb_h));

    cairo_rectangle(cr, bar_x, thumb_y, bar_w, thumb_h);
    cairo_set_source_rgba(cr, 0.78, 0.78, 0.78, 0.5);
    cairo_fill(cr);
  }

  g_object_unref(layout);

  /* Reset global dirty */
  GhosttyRenderStateDirty clean_state = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
  ghostty_render_state_set(self->render_state,
    GHOSTTY_RENDER_STATE_OPTION_DIRTY, &clean_state);
}

/* ------------------------------------------------------------------ */
/* Cursor blink timer                                                  */
/* ------------------------------------------------------------------ */

static gboolean on_cursor_tick(gpointer user_data) {
  SbTerminal *self = user_data;
  self->cursor_visible = !self->cursor_visible;
  gtk_widget_queue_draw(self->widget);
  return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

SbTerminal *sb_terminal_new(void) {
  SbTerminal *self = g_malloc0(sizeof(SbTerminal));

  self->font_size = 14;
  self->padding = 4;
  self->cols = 80;
  self->rows = 24;
  self->cursor_visible = true;

  /* Font */
  self->font_desc = pango_font_description_new();
  pango_font_description_set_family(self->font_desc, "JetBrains Mono, Monospace");
  pango_font_description_set_size(self->font_desc, self->font_size * PANGO_SCALE);
  pango_font_description_set_weight(self->font_desc, PANGO_WEIGHT_NORMAL);

  /* Measure cell size using a temporary widget */
  self->widget = gtk_drawing_area_new();
  gtk_widget_set_focusable(self->widget, TRUE);
  PangoLayout *tmp = gtk_widget_create_pango_layout(self->widget, "M");
  pango_layout_set_font_description(tmp, self->font_desc);
  pango_layout_get_pixel_size(tmp, &self->cell_width, &self->cell_height);
  g_object_unref(tmp);

  /* Terminal */
  GhosttyTerminalOptions opts = {
    .cols = self->cols,
    .rows = self->rows,
    .max_scrollback = 100000,
  };
  ghostty_terminal_new(NULL, &self->terminal, opts);

  /* Set up effects */
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_USERDATA, self);
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_WRITE_PTY, &effect_write_pty);
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_TITLE_CHANGED, &effect_title_changed);
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_SIZE, &effect_size);
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES, &effect_device_attributes);
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_XTVERSION, &effect_xtversion);
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_COLOR_SCHEME, &effect_color_scheme);

  /* Set default colors */
  GhosttyColorRgb fg = {0xEA, 0xEA, 0xEA};
  GhosttyColorRgb bg = {0x1E, 0x1E, 0x1E};
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &fg);
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &bg);

  /* Render state */
  ghostty_render_state_new(NULL, &self->render_state);
  ghostty_render_state_update(self->render_state, self->terminal);

  /* Row iterator and cells */
  ghostty_render_state_row_iterator_new(NULL, &self->row_iter);
  ghostty_render_state_row_cells_new(NULL, &self->cells);

  /* Key encoder and event */
  ghostty_key_encoder_new(NULL, &self->key_encoder);
  ghostty_key_event_new(NULL, &self->key_event);

  /* Mouse encoder and event */
  ghostty_mouse_encoder_new(NULL, &self->mouse_encoder);
  ghostty_mouse_event_new(NULL, &self->mouse_event);

  /* PTY */
  self->pty_fd = pty_spawn(&self->child_pid, self->cols, self->rows,
                           self->cell_width, self->cell_height);
  if (self->pty_fd < 0) {
    g_warning("sb_terminal: failed to spawn PTY");
  } else {
    GIOChannel *channel = g_io_channel_unix_new(self->pty_fd);
    self->pty_source = g_io_add_watch(channel,
      G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
      on_pty_readable, self);
    g_io_channel_unref(channel);
  }

  /* Drawing area setup */
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->widget),
    render_terminal, self, NULL);

  /* Keyboard controller on the drawing area itself */
  GtkEventController *k = gtk_event_controller_key_new();
  g_signal_connect(k, "key-pressed", G_CALLBACK(on_terminal_key), self);
  gtk_widget_add_controller(self->widget, k);

  /* Scroll controller */
  GtkEventController *scroll_controller =
    gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(scroll_controller, "scroll",
    G_CALLBACK(on_scroll), self);
  gtk_widget_add_controller(self->widget, scroll_controller);

  /* Cursor blink timer (every 500ms) */
  self->cursor_timer = g_timeout_add(500, on_cursor_tick, self);

  return self;
}

void sb_terminal_free(SbTerminal *self) {
  if (!self) return;

  if (self->cursor_timer > 0)
    g_source_remove(self->cursor_timer);

  if (self->pty_source > 0)
    g_source_remove(self->pty_source);

  if (self->pty_fd >= 0)
    close(self->pty_fd);

  if (self->child_pid > 0)
    kill(self->child_pid, SIGTERM);

  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->widget),
    NULL, NULL, NULL);

  ghostty_mouse_event_free(self->mouse_event);
  ghostty_mouse_encoder_free(self->mouse_encoder);
  ghostty_key_event_free(self->key_event);
  ghostty_key_encoder_free(self->key_encoder);
  ghostty_render_state_row_cells_free(self->cells);
  ghostty_render_state_row_iterator_free(self->row_iter);
  ghostty_render_state_free(self->render_state);
  ghostty_terminal_free(self->terminal);

  pango_font_description_free(self->font_desc);
  g_free(self);
}

GtkWidget *sb_terminal_get_widget(SbTerminal *self) {
  return self->widget;
}

int sb_terminal_get_pty_fd(SbTerminal *self) {
  return self->pty_fd;
}

void sb_terminal_write(SbTerminal *self, const char *data, size_t len) {
  if (self->pty_fd >= 0)
    pty_write_raw(self->pty_fd, data, len);
}

void sb_terminal_write_str(SbTerminal *self, const char *str) {
  sb_terminal_write(self, str, strlen(str));
}

void sb_terminal_set_title_callback(SbTerminal *self, SbTerminalTitleCb cb, void *userdata) {
  self->title_cb = cb;
  self->title_cb_data = userdata;
}
