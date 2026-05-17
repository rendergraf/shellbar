/*
 * ShellBar v1.3 — A command-bar terminal emulator built on libghostty-vt
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

  guint scroll_anim_timer;
  guint follow_timer;
  bool follow_bottom;

  SbTerminalTitleCb title_cb;
  void *title_cb_data;

  char last_title[256];

  int sel_start_col, sel_start_row;
  int sel_end_col, sel_end_row;
  bool has_selection;
  bool selecting;

  SbConfigKeybind *keybinds;
  int keybind_count;

  GtkGesture *right_click_gesture;

  GtkWidget *context_menu;

  bool button_held;
  bool mouse_tracking_click;
  int press_col, press_row;

  GtkEventController *motion_controller;
  int hover_col, hover_row;
  char *hover_url;
  int hover_url_start_col, hover_url_end_col, hover_url_row;
  bool has_hover_url;
};

/* ------------------------------------------------------------------ */
/* Selection helpers                                                    */
/* ------------------------------------------------------------------ */

static void selection_clear(SbTerminal *self) {
  self->has_selection = false;
  self->selecting = false;
  gtk_widget_queue_draw(self->widget);
}

static void selection_from_cell_coords(SbTerminal *self,
                                        int col_a, int row_a,
                                        int col_b, int row_b) {
  if (col_a < col_b || (col_a == col_b && row_a < row_b)) {
    self->sel_start_col = col_a; self->sel_start_row = row_a;
    self->sel_end_col = col_b;   self->sel_end_row = row_b;
  } else {
    self->sel_start_col = col_b; self->sel_start_row = row_b;
    self->sel_end_col = col_a;   self->sel_end_row = row_a;
  }
  if (self->sel_start_col < 0) self->sel_start_col = 0;
  if (self->sel_start_row < 0) self->sel_start_row = 0;
  if (self->sel_end_col >= (int)self->cols) self->sel_end_col = self->cols - 1;
  if (self->sel_end_row >= (int)self->rows) self->sel_end_row = self->rows - 1;
  self->has_selection = true;
}

static bool cell_is_selected(SbTerminal *self, int col, int row) {
  if (!self->has_selection) return false;
  if (row < self->sel_start_row || row > self->sel_end_row) return false;
  if (row == self->sel_start_row && col < self->sel_start_col) return false;
  if (row == self->sel_end_row && col > self->sel_end_col) return false;
  return true;
}

static char *selection_get_text(SbTerminal *self) {
  if (!self->has_selection) return NULL;

  GString *result = g_string_new("");
  int prev_row = -1;

  if (ghostty_render_state_get(self->render_state,
        GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &self->row_iter)
      != GHOSTTY_SUCCESS)
    return g_string_free(result, FALSE);

  int row_idx = 0;
  while (ghostty_render_state_row_iterator_next(self->row_iter)) {
    if (row_idx < self->sel_start_row) { row_idx++; continue; }
    if (row_idx > self->sel_end_row) break;

    if (ghostty_render_state_row_get(self->row_iter,
          GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &self->cells)
        != GHOSTTY_SUCCESS) { row_idx++; continue; }

    if (prev_row >= 0 && prev_row != row_idx - 1)
      g_string_append_c(result, '\n');
    else if (prev_row >= 0)
      g_string_append_c(result, '\n');

    int col_idx = 0;
    int last_selected_col = -1;
    while (ghostty_render_state_row_cells_next(self->cells)) {
      if (col_idx < self->sel_start_col && row_idx == self->sel_start_row)
        { col_idx++; continue; }
      if (col_idx > self->sel_end_col && row_idx == self->sel_end_row)
        break;
      if (!cell_is_selected(self, col_idx, row_idx))
        { col_idx++; continue; }

      uint32_t grapheme_len = 0;
      ghostty_render_state_row_cells_get(self->cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);

      if (grapheme_len > 0) {
        last_selected_col = col_idx;
        uint32_t codepoints[16];
        uint32_t len = grapheme_len < 16 ? grapheme_len : 16;
        ghostty_render_state_row_cells_get(self->cells,
          GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, codepoints);
        for (uint32_t i = 0; i < len; i++) {
          char utf8[8] = {0};
          int n = g_unichar_to_utf8((gunichar)codepoints[i], utf8);
          if (n > 0) g_string_append_len(result, utf8, n);
        }
      }
      col_idx++;
    }
    if (last_selected_col < self->sel_end_col && row_idx == self->sel_end_row) {
      /* already covered by the check above */
    }
    prev_row = row_idx;
    row_idx++;
  }

  if (result->len == 0) {
    g_string_free(result, TRUE);
    return NULL;
  }
  return g_string_free(result, FALSE);
}

/* ------------------------------------------------------------------ */
/* Clipboard helpers                                                    */
/* ------------------------------------------------------------------ */

static void copy_to_clipboard(SbTerminal *self) {
  char *text = selection_get_text(self);
  if (!text) return;
  GdkClipboard *clipboard = gtk_widget_get_clipboard(self->widget);
  gdk_clipboard_set_text(clipboard, text);
  g_free(text);
}

static void paste_callback(GObject *source, GAsyncResult *result,
                           gpointer user_data) {
  SbTerminal *self = user_data;
  GdkClipboard *clipboard = GDK_CLIPBOARD(source);
  GError *error = NULL;
  char *text = gdk_clipboard_read_text_finish(clipboard, result, &error);
  if (text) {
    bool bracketed = false;
    ghostty_terminal_mode_get(self->terminal,
      GHOSTTY_MODE_BRACKETED_PASTE, &bracketed);
    if (bracketed) {
      char buf[65536];
      size_t written = 0;
      ghostty_paste_encode(text, strlen(text), true,
                          buf, sizeof(buf), &written);
      if (written > 0)
        sb_terminal_write(self, buf, written);
    } else {
      sb_terminal_write_str(self, text);
    }
    g_free(text);
  }
  g_clear_error(&error);
}

static void paste_from_clipboard(SbTerminal *self) {
  GdkClipboard *clipboard = gtk_widget_get_clipboard(self->widget);
  gdk_clipboard_read_text_async(clipboard, NULL, paste_callback, self);
}

/* ------------------------------------------------------------------ */
/* Context menu                                                         */
/* ------------------------------------------------------------------ */

static void menu_copy(GtkWidget *item, gpointer user_data) {
  (void)item;
  copy_to_clipboard((SbTerminal *)user_data);
}

static void menu_paste(GtkWidget *item, gpointer user_data) {
  (void)item;
  paste_from_clipboard((SbTerminal *)user_data);
}

static void menu_select_all(GtkWidget *item, gpointer user_data) {
  (void)item;
  sb_terminal_select_all((SbTerminal *)user_data);
}

static GtkWidget *create_context_menu(SbTerminal *self) {
  GMenu *menu = g_menu_new();

  g_menu_append(menu, "Copy", "menu.copy");
  g_menu_append(menu, "Paste", "menu.paste");
  g_menu_append(menu, "Select All", "menu.selectall");

  GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
  g_object_unref(menu);
  gtk_widget_set_parent(popover, self->widget);

  GSimpleActionGroup *actions = g_simple_action_group_new();
  GActionEntry entries[] = {
    { "copy",   NULL, NULL, NULL, NULL, {0} },
    { "paste",  NULL, NULL, NULL, NULL, {0} },
    { "selectall", NULL, NULL, NULL, NULL, {0} },
  };
  g_action_map_add_action_entries(G_ACTION_MAP(actions), entries,
    G_N_ELEMENTS(entries), self);
  gtk_widget_insert_action_group(self->widget, "menu",
                                  G_ACTION_GROUP(actions));
  g_object_unref(actions);

  g_signal_connect(self->widget, "action-activated::menu.copy",
                   G_CALLBACK(menu_copy), self);
  g_signal_connect(self->widget, "action-activated::menu.paste",
                   G_CALLBACK(menu_paste), self);
  g_signal_connect(self->widget, "action-activated::menu.selectall",
                   G_CALLBACK(menu_select_all), self);

  return popover;
}

/* ------------------------------------------------------------------ */
/* URL detection / open                                                 */
/* ------------------------------------------------------------------ */

/* Read the full visible text of a single row. byte_for_col[i] (if non-NULL)
 * receives the byte offset within the returned string for the start of cell
 * column i (0..cols). The terminator slot byte_for_col[cols] is set to the
 * total byte length. */
static char *read_row_text(SbTerminal *self, int target_row,
                           int *byte_for_col) {
  if (target_row < 0 || target_row >= (int)self->rows) return NULL;

  GString *out = g_string_new("");
  if (byte_for_col)
    for (int i = 0; i <= (int)self->cols; i++) byte_for_col[i] = 0;

  if (ghostty_render_state_get(self->render_state,
        GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &self->row_iter)
      != GHOSTTY_SUCCESS)
    return g_string_free(out, FALSE);

  int row_idx = 0;
  while (ghostty_render_state_row_iterator_next(self->row_iter)) {
    if (row_idx != target_row) { row_idx++; continue; }

    if (ghostty_render_state_row_get(self->row_iter,
          GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &self->cells)
        != GHOSTTY_SUCCESS) break;

    int col_idx = 0;
    while (ghostty_render_state_row_cells_next(self->cells)) {
      if (byte_for_col && col_idx <= (int)self->cols)
        byte_for_col[col_idx] = (int)out->len;

      uint32_t grapheme_len = 0;
      ghostty_render_state_row_cells_get(self->cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);
      if (grapheme_len > 0) {
        uint32_t codepoints[16];
        uint32_t len = grapheme_len < 16 ? grapheme_len : 16;
        ghostty_render_state_row_cells_get(self->cells,
          GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, codepoints);
        for (uint32_t i = 0; i < len; i++) {
          char utf8[8] = {0};
          int n = g_unichar_to_utf8((gunichar)codepoints[i], utf8);
          if (n > 0) g_string_append_len(out, utf8, n);
        }
      } else {
        g_string_append_c(out, ' ');
      }
      col_idx++;
      if (col_idx >= (int)self->cols) break;
    }
    /* fill remaining with spaces and advance byte_for_col */
    while (col_idx < (int)self->cols) {
      if (byte_for_col) byte_for_col[col_idx] = (int)out->len;
      g_string_append_c(out, ' ');
      col_idx++;
    }
    if (byte_for_col) byte_for_col[self->cols] = (int)out->len;
    break;
  }

  return g_string_free(out, FALSE);
}

/* True if c is a valid character for the body of a URL. */
static bool url_char(unsigned char c) {
  if (c <= 0x20 || c >= 0x7F) return false;
  switch (c) {
    case '<': case '>': case '"': case '\'':
    case '`': case ' ': case '\t':
    case ',': case ';':
    case ')': case ']': case '}':
      return false;
    default:
      return true;
  }
}

/* Find a URL in `text` that contains byte offset `pos`. Returns a newly
 * allocated string or NULL. If non-NULL, *out_start and *out_end receive the
 * byte offset of the URL start and the byte after the URL end. */
static char *find_url_at_byte(const char *text, int pos,
                              int *out_start, int *out_end) {
  if (!text || pos < 0) return NULL;
  int len = (int)strlen(text);
  if (pos >= len) return NULL;

  /* Find scheme starts to the left. We accept http/https/ftp/file/ssh/mailto. */
  static const char *const schemes[] = {
    "https://", "http://", "ftp://", "ftps://",
    "file://", "ssh://", "mailto:", NULL
  };

  /* Walk backwards looking for any scheme token that, plus its body, covers pos. */
  for (int start = pos; start >= 0; start--) {
    for (int s = 0; schemes[s]; s++) {
      int slen = (int)strlen(schemes[s]);
      if (start + slen > len) continue;
      if (g_ascii_strncasecmp(text + start, schemes[s], slen) != 0) continue;

      int end = start + slen;
      while (end < len && url_char((unsigned char)text[end])) end++;
      /* Trim trailing punctuation that often follows URLs in prose. */
      while (end > start + slen) {
        char last = text[end - 1];
        if (last == '.' || last == ',' || last == ';' ||
            last == ':' || last == '!' || last == '?')
          end--;
        else break;
      }
      if (end <= pos) continue;
      if (end - start <= slen) continue;
      if (out_start) *out_start = start;
      if (out_end)   *out_end   = end;
      return g_strndup(text + start, end - start);
    }
  }
  return NULL;
}

/* Resolve URL at terminal cell (col,row), returns newly allocated string. */
static char *url_at_cell(SbTerminal *self, int col, int row) {
  int *map = g_malloc((self->cols + 1) * sizeof(int));
  char *line = read_row_text(self, row, map);
  char *url = NULL;
  if (line && col >= 0 && col < (int)self->cols)
    url = find_url_at_byte(line, map[col], NULL, NULL);
  g_free(line);
  g_free(map);
  return url;
}

typedef struct {
  char *url;
  int start_col;
  int end_col;
} UrlCellRange;

/* Like url_at_cell but also returns the column span of the URL.
   Returns true if a URL was found. range->url must be freed by caller. */
static bool url_range_at_cell(SbTerminal *self, int col, int row,
                              UrlCellRange *range) {
  memset(range, 0, sizeof(*range));

  int *map = g_malloc((self->cols + 1) * sizeof(int));
  char *line = read_row_text(self, row, map);

  if (!line || col < 0 || col >= (int)self->cols) {
    g_free(line);
    g_free(map);
    return false;
  }

  int byte_start = 0, byte_end = 0;
  char *url = find_url_at_byte(line, map[col], &byte_start, &byte_end);
  if (!url) {
    g_free(line);
    g_free(map);
    return false;
  }

  /* Map byte offsets back to column indices. start_col gets the column
     whose byte offset is ≤ byte_start; end_col the column whose byte
     offset is < byte_end. */
  range->start_col = 0;
  range->end_col = 0;
  for (int i = 0; i < (int)self->cols; i++) {
    if (map[i] <= byte_start)  range->start_col = i;
    if (map[i] < byte_end)     range->end_col   = i;
  }

  range->url = url;
  g_free(line);
  g_free(map);
  return true;
}

static void open_url(const char *url) {
  if (!url || !*url) return;
  char *full = NULL;
  if (g_ascii_strncasecmp(url, "mailto:", 7) == 0) {
    full = g_strdup(url);
  } else {
    full = g_strdup(url);
  }
  GError *err = NULL;
  g_app_info_launch_default_for_uri(full, NULL, &err);
  if (err) {
    g_warning("sb_terminal: failed to open URL '%s': %s", full, err->message);
    g_error_free(err);
  }
  g_free(full);
}

/* ------------------------------------------------------------------ */
/* Mouse gesture callbacks                                              */
/* ------------------------------------------------------------------ */

/* Forward declaration — defined later in the PTY section */
static void pty_write_raw(int fd, const char *buf, size_t len);

/* Forward a mouse event to the PTY when the terminal has mouse tracking
 * enabled.  Returns true if the event was forwarded. */
static bool send_mouse_event_to_pty(SbTerminal *self,
                                     GhosttyMouseButton button,
                                     GhosttyMouseAction action,
                                     double x, double y,
                                     GdkModifierType state) {
  bool mouse_tracking = false;
  ghostty_terminal_get(self->terminal,
    GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse_tracking);
  if (!mouse_tracking)
    return false;

  ghostty_mouse_encoder_setopt_from_terminal(self->mouse_encoder,
                                             self->terminal);

  GhosttyMouseEncoderSize size_ctx = {
    .size = sizeof(GhosttyMouseEncoderSize),
    .screen_width  = (uint32_t)self->last_alloc_w,
    .screen_height = (uint32_t)self->last_alloc_h,
    .cell_width    = (uint32_t)self->cell_width,
    .cell_height   = (uint32_t)self->cell_height,
    .padding_top   = (uint32_t)self->padding,
    .padding_bottom= (uint32_t)self->padding,
    .padding_right = (uint32_t)self->padding,
    .padding_left  = (uint32_t)self->padding,
  };
  ghostty_mouse_encoder_setopt(self->mouse_encoder,
    GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size_ctx);

  ghostty_mouse_event_set_action(self->mouse_event, action);
  ghostty_mouse_event_set_button(self->mouse_event, button);
  ghostty_mouse_event_set_position(self->mouse_event,
    (GhosttyMousePosition){ .x = (float)x, .y = (float)y });

  GhosttyMods mods = 0;
  if (state & GDK_SHIFT_MASK)   mods |= GHOSTTY_MODS_SHIFT;
  if (state & GDK_CONTROL_MASK) mods |= GHOSTTY_MODS_CTRL;
  if (state & GDK_ALT_MASK)     mods |= GHOSTTY_MODS_ALT;
  if (state & GDK_SUPER_MASK)   mods |= GHOSTTY_MODS_SUPER;
  ghostty_mouse_event_set_mods(self->mouse_event, mods);

  char buf[128];
  size_t written = 0;
  ghostty_mouse_encoder_encode(self->mouse_encoder, self->mouse_event,
                               buf, sizeof(buf), &written);
  if (written > 0)
    pty_write_raw(self->pty_fd, buf, written);
  return true;
}

static void right_click_pressed(GtkGestureClick *gesture, int n_press,
                                double x, double y, gpointer user_data) {
  SbTerminal *self = user_data;
  (void)n_press;

  GdkModifierType state =
    gtk_event_controller_get_current_event_state(
      GTK_EVENT_CONTROLLER(gesture));
  if (send_mouse_event_to_pty(self, GHOSTTY_MOUSE_BUTTON_RIGHT,
                               GHOSTTY_MOUSE_ACTION_PRESS, x, y, state)) {
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    return;
  }

  if (!self->context_menu)
    self->context_menu = create_context_menu(self);
  GdkRectangle rect = { (int)x, (int)y, 1, 1 };
  gtk_popover_set_pointing_to(GTK_POPOVER(self->context_menu), &rect);
  gtk_popover_popup(GTK_POPOVER(self->context_menu));
}

/* Middle-click pastes from clipboard unless mouse tracking is active. */
static void middle_click_pressed(GtkGestureClick *gesture, int n_press,
                                 double x, double y, gpointer user_data) {
  (void)n_press;
  SbTerminal *self = user_data;

  GdkModifierType state =
    gtk_event_controller_get_current_event_state(
      GTK_EVENT_CONTROLLER(gesture));
  if (send_mouse_event_to_pty(self, GHOSTTY_MOUSE_BUTTON_MIDDLE,
                               GHOSTTY_MOUSE_ACTION_PRESS, x, y, state)) {
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    return;
  }

  paste_from_clipboard(self);
}

/* Word-character test used for double-click word selection. */
static bool is_word_char(unsigned char c) {
  return g_ascii_isalnum(c) || c == '_' || c == '-' || c == '.';
}

/* Select the word at (col, row). */
static void select_word_at(SbTerminal *self, int col, int row) {
  int *map = g_malloc((self->cols + 1) * sizeof(int));
  char *line = read_row_text(self, row, map);
  if (!line) { g_free(map); return; }

  int byte_pos = map[col];
  int len = (int)strlen(line);

  int ws = byte_pos;
  while (ws > 0 && is_word_char((unsigned char)line[ws - 1])) ws--;

  int we = byte_pos;
  while (we < len && is_word_char((unsigned char)line[we])) we++;

  if (ws == we) { g_free(line); g_free(map); return; }

  self->sel_start_col = 0;
  self->sel_start_row = row;
  self->sel_end_col = 0;
  self->sel_end_row = row;
  for (int i = 0; i < (int)self->cols; i++) {
    if (map[i] <= ws) self->sel_start_col = i;
    if (map[i] <  we) self->sel_end_col   = i;
  }
  self->has_selection = true;

  g_free(line);
  g_free(map);
}

/* Combined left-click handler.
   - triple-click → select line
   - double-click → select word
   - Ctrl+click  → open URL
   - Shift+click → extend selection
   - single click → initiate drag selection via on_motion */
static void left_click_pressed(GtkGestureClick *gesture, int n_press,
                                double x, double y, gpointer user_data) {
  SbTerminal *self = user_data;

  int col = (int)((x - self->padding) / self->cell_width);
  int row = (int)((y - self->padding) / self->cell_height);
  if (col < 0) col = 0;
  if (row < 0) row = 0;
  if (col >= (int)self->cols) col = self->cols - 1;
  if (row >= (int)self->rows) row = self->rows - 1;

  self->button_held = true;
  self->press_col = col;
  self->press_row = row;

  GdkModifierType state =
    gtk_event_controller_get_current_event_state(
      GTK_EVENT_CONTROLLER(gesture));

  /* When mouse tracking is enabled in the terminal (e.g. by btop, htop,
   * nvim, lazygit), forward mouse events to the PTY so the application
   * can handle them interactively. */
  if (send_mouse_event_to_pty(self, GHOSTTY_MOUSE_BUTTON_LEFT,
                               GHOSTTY_MOUSE_ACTION_PRESS, x, y, state)) {
    self->mouse_tracking_click = true;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    return;
  }
  self->mouse_tracking_click = false;

  /* Triple-click → select entire line */
  if (n_press >= 3) {
    self->sel_start_col = 0;
    self->sel_start_row = row;
    self->sel_end_col = self->cols - 1;
    self->sel_end_row = row;
    self->has_selection = true;
    self->selecting = false;
    copy_to_clipboard(self);
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_widget_queue_draw(self->widget);
    return;
  }

  /* Double-click → select word */
  if (n_press == 2) {
    select_word_at(self, col, row);
    self->selecting = false;
    if (self->has_selection)
      copy_to_clipboard(self);
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_widget_queue_draw(self->widget);
    return;
  }

  /* Ctrl+click → open URL */
  if (state & GDK_CONTROL_MASK) {
    char *url = url_at_cell(self, col, row);
    if (url) {
      open_url(url);
      g_free(url);
      self->selecting = false;
      gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
      return;
    }
  }

  /* Shift+click → extend selection */
  if (state & GDK_SHIFT_MASK) {
    if (!self->has_selection) {
      uint16_t cx = 0, cy = 0;
      ghostty_render_state_get(self->render_state,
        GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
      ghostty_render_state_get(self->render_state,
        GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);
      selection_from_cell_coords(self, cx, cy, col, row);
    } else {
      selection_from_cell_coords(self, self->sel_start_col,
        self->sel_start_row, col, row);
    }
    self->selecting = false;
    copy_to_clipboard(self);
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_widget_queue_draw(self->widget);
    return;
  }

  /* Plain single click: prepare for potential drag selection */
  self->selecting = true;
  self->sel_start_col = self->sel_end_col = col;
  self->sel_start_row = self->sel_end_row = row;
  self->has_selection = false;
  gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void left_click_released(GtkGestureClick *gesture, int n_press,
                                 double x, double y, gpointer user_data) {
  (void)n_press;
  SbTerminal *self = user_data;

  self->button_held = false;

  if (self->mouse_tracking_click) {
    self->mouse_tracking_click = false;
    GdkModifierType state =
      gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(gesture));
    send_mouse_event_to_pty(self, GHOSTTY_MOUSE_BUTTON_LEFT,
                             GHOSTTY_MOUSE_ACTION_RELEASE, x, y, state);
    return;
  }

  if (!self->selecting) return;
  self->selecting = false;

  if (self->has_selection)
    copy_to_clipboard(self);
  else
    selection_clear(self);

  gtk_widget_queue_draw(self->widget);
}

/* ------------------------------------------------------------------ */
/* Motion / hover callbacks                                             */
/* ------------------------------------------------------------------ */

static gboolean on_query_tooltip(GtkWidget *widget, gint x, gint y,
                                  gboolean keyboard_mode, GtkTooltip *tooltip,
                                  gpointer user_data) {
  (void)widget;
  (void)x;
  (void)y;
  (void)keyboard_mode;
  SbTerminal *self = user_data;

  if (self->has_hover_url) {
    gtk_tooltip_set_text(tooltip, "Ctrl+Click to open link");
    return TRUE;
  }
  return FALSE;
}

static void on_motion_enter(GtkEventControllerMotion *controller,
                            gdouble x, gdouble y, gpointer user_data) {
  (void)controller;
  SbTerminal *self = user_data;

  int col = (int)((x - self->padding) / self->cell_width);
  int row = (int)((y - self->padding) / self->cell_height);

  if (col >= 0 && row >= 0 &&
      col < (int)self->cols && row < (int)self->rows) {
    GdkCursor *cursor = gdk_cursor_new_from_name("text", NULL);
    gtk_widget_set_cursor(self->widget, cursor);
    g_object_unref(cursor);
  }
}

static void on_motion(GtkEventControllerMotion *controller,
                      gdouble x, gdouble y, gpointer user_data) {
  (void)controller;
  SbTerminal *self = user_data;

  int col = (int)((x - self->padding) / self->cell_width);
  int row = (int)((y - self->padding) / self->cell_height);

  /* Mouse-tracking-mode motion: forward drag to PTY */
  if (self->button_held && self->mouse_tracking_click) {
    send_mouse_event_to_pty(self, GHOSTTY_MOUSE_BUTTON_LEFT,
                             GHOSTTY_MOUSE_ACTION_MOTION, x, y, 0);
    return;
  }

  /* Drag selection (button held + selecting mode) */
  if (self->button_held && self->selecting) {
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= (int)self->cols) col = self->cols - 1;
    if (row >= (int)self->rows) row = self->rows - 1;
    selection_from_cell_coords(self, self->press_col, self->press_row,
                                col, row);
    gtk_widget_queue_draw(self->widget);
    return;
  }

  if (col == self->hover_col && row == self->hover_row)
    return;

  self->hover_col = col;
  self->hover_row = row;

  g_free(self->hover_url);
  self->hover_url = NULL;
  self->has_hover_url = false;

  bool in_bounds = (col >= 0 && row >= 0 &&
                    col < (int)self->cols && row < (int)self->rows);

  if (in_bounds) {
    UrlCellRange range;
    if (url_range_at_cell(self, col, row, &range)) {
      self->hover_url = range.url;
      self->hover_url_start_col = range.start_col;
      self->hover_url_end_col = range.end_col;
      self->hover_url_row = row;
      self->has_hover_url = true;
    }
  }

  const char *cursor_name = NULL;
  if (self->has_hover_url)
    cursor_name = "pointer";
  else if (in_bounds)
    cursor_name = "text";

  if (cursor_name) {
    GdkCursor *cursor = gdk_cursor_new_from_name(cursor_name, NULL);
    gtk_widget_set_cursor(self->widget, cursor);
    g_object_unref(cursor);
  } else {
    gtk_widget_set_cursor(self->widget, NULL);
  }

  gtk_widget_queue_draw(self->widget);
}

static void on_motion_leave(GtkEventControllerMotion *controller,
                            gpointer user_data) {
  (void)controller;
  SbTerminal *self = user_data;

  if (self->has_hover_url) {
    self->has_hover_url = false;
    g_free(self->hover_url);
    self->hover_url = NULL;
  }
  gtk_widget_set_cursor(self->widget, NULL);
  gtk_widget_queue_draw(self->widget);
}

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
/* Auto-scroll animation                                                */
/* ------------------------------------------------------------------ */

static gboolean on_scroll_animate(gpointer user_data) {
  SbTerminal *self = user_data;

  GhosttyTerminalScrollbar scrollbar = {0};
  if (ghostty_terminal_get(self->terminal,
        GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) != GHOSTTY_SUCCESS
      || scrollbar.total <= scrollbar.len) {
    self->scroll_anim_timer = 0;
    return G_SOURCE_REMOVE;
  }

  int bottom = (int)(scrollbar.total - scrollbar.len);
  if (scrollbar.offset >= bottom) {
    self->scroll_anim_timer = 0;
    return G_SOURCE_REMOVE;
  }

  int remaining = bottom - (int)scrollbar.offset;
  int step = remaining / 3;
  if (step < 1)  step = 1;
  if (step > 50) step = 50;

  GhosttyTerminalScrollViewport sv = {
    .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
    .value = { .delta = step },
  };
  ghostty_terminal_scroll_viewport(self->terminal, sv);
  ghostty_render_state_update(self->render_state, self->terminal);
  gtk_widget_queue_draw(self->widget);

  return G_SOURCE_CONTINUE;
}

static gboolean clear_follow(gpointer user_data) {
  SbTerminal *self = user_data;
  self->follow_bottom = false;
  self->follow_timer = 0;
  return G_SOURCE_REMOVE;
}

static void auto_scroll_to_bottom(SbTerminal *self) {
  GhosttyTerminalScrollbar scrollbar = {0};
  if (ghostty_terminal_get(self->terminal,
        GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) != GHOSTTY_SUCCESS)
    return;

  if (scrollbar.total <= scrollbar.len)
    return;

  int bottom = (int)(scrollbar.total - scrollbar.len);
  if (scrollbar.offset >= bottom)
    return;

  if (self->scroll_anim_timer > 0)
    g_source_remove(self->scroll_anim_timer);

  self->scroll_anim_timer = g_timeout_add(16, on_scroll_animate, self);
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

static bool get_cwd_basename(pid_t pid, char *out, size_t out_size) {
  char link_path[64];
  snprintf(link_path, sizeof(link_path), "/proc/%d/cwd", (int)pid);
  char buf[4096];
  ssize_t n = readlink(link_path, buf, sizeof(buf) - 1);
  if (n <= 0) return false;
  buf[n] = '\0';

  const char *name = buf;
  for (ssize_t i = 0; i < n; i++) {
    if (buf[i] == '/') name = buf + i + 1;
  }
  if (*name == '\0') name = buf;

  size_t name_len = strlen(name);
  if (name_len == 0) return false;

  if (name_len <= 16) {
    size_t cp = name_len < out_size - 1 ? name_len : out_size - 1;
    memcpy(out, name, cp);
    out[cp] = '\0';
  } else {
    size_t keep = 13;
    if (out_size < 4) keep = out_size - 4;
    if (out_size > 3) memcpy(out, "...", 3);
    size_t skip = name_len - keep;
    size_t cp = keep < out_size - 3 ? keep : out_size - 3;
    memcpy(out + 3, name + skip, cp);
    out[3 + cp] = '\0';
  }
  return true;
}

static void format_tab_title_from_pwd(const GhosttyString *pwd,
                                       char *out, size_t out_size) {
  const char *uri = (const char *)pwd->ptr;
  size_t uri_len = pwd->len;

  const char *path_start = NULL;
  if (uri_len >= 7 && memcmp(uri, "file://", 7) == 0) {
    const char *host_end = memchr(uri + 7, '/', uri_len - 7);
    if (host_end)
      path_start = host_end;
    else
      path_start = uri + 7;
  } else {
    path_start = uri;
  }

  const char *name = path_start;
  size_t path_len = uri_len - (size_t)(path_start - uri);
  for (size_t i = 0; i < path_len; i++) {
    if (path_start[i] == '/')
      name = path_start + i + 1;
  }
  if (*name == '\0')
    name = path_start;

  size_t name_len = uri_len - (size_t)(name - uri);
  if (name_len == 0) {
    if (out_size > 0) out[0] = '\0';
    return;
  }

  if (name_len <= 16) {
    size_t cp = name_len < out_size - 1 ? name_len : out_size - 1;
    memcpy(out, name, cp);
    out[cp] = '\0';
  } else {
    size_t keep = 13;
    if (out_size < 4) keep = out_size - 4;
    if (out_size > 3) memcpy(out, "...", 3);
    size_t skip = name_len - keep;
    size_t cp = keep < out_size - 3 ? keep : out_size - 3;
    memcpy(out + 3, name + skip, cp);
    out[3 + cp] = '\0';
  }
}

static void effect_title_changed(GhosttyTerminal terminal, void *userdata) {
  SbTerminal *self = userdata;

  GhosttyString pwd = {0};
  if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_PWD, &pwd)
      == GHOSTTY_SUCCESS && pwd.len > 0) {
    char title[256] = {0};
    format_tab_title_from_pwd(&pwd, title, sizeof(title));
    if (title[0] && strcmp(title, self->last_title) != 0) {
      strncpy(self->last_title, title, sizeof(self->last_title) - 1);
      if (self->title_cb)
        self->title_cb(self, title, self->title_cb_data);
    }
    return;
  }

  GhosttyString osc_title = {0};
  if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_TITLE, &osc_title)
      != GHOSTTY_SUCCESS || osc_title.len == 0)
    return;
  char buf[256];
  size_t len = osc_title.len < sizeof(buf) - 1 ? osc_title.len : sizeof(buf) - 1;
  memcpy(buf, osc_title.ptr, len);
  buf[len] = '\0';
  if (strcmp(buf, self->last_title) != 0) {
    strncpy(self->last_title, buf, sizeof(self->last_title) - 1);
    if (self->title_cb)
      self->title_cb(self, buf, self->title_cb_data);
  }
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
  return (GhosttyString){ .ptr = (const uint8_t *)"shellbar 1.3", .len = 13 };
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

    GhosttyString pwd = {0};
    bool title_updated = false;
    if (ghostty_terminal_get(self->terminal,
          GHOSTTY_TERMINAL_DATA_PWD, &pwd) == GHOSTTY_SUCCESS
        && pwd.len > 0) {
      char title[256] = {0};
      format_tab_title_from_pwd(&pwd, title, sizeof(title));
      if (title[0] && strcmp(title, self->last_title) != 0) {
        strncpy(self->last_title, title, sizeof(self->last_title) - 1);
        if (self->title_cb)
          self->title_cb(self, title, self->title_cb_data);
        title_updated = true;
      }
    }

    if (!title_updated && self->child_pid > 0) {
      char title[256] = {0};
      if (get_cwd_basename(self->child_pid, title, sizeof(title))
          && title[0] && strcmp(title, self->last_title) != 0) {
        strncpy(self->last_title, title, sizeof(self->last_title) - 1);
        if (self->title_cb)
          self->title_cb(self, title, self->title_cb_data);
      }
    }

    if (self->follow_bottom) {
      self->follow_bottom = false;
      if (self->follow_timer > 0) {
        g_source_remove(self->follow_timer);
        self->follow_timer = 0;
      }
      auto_scroll_to_bottom(self);
    }

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

  /* Normalize keyval (Shift+c arrives as 'C'); compare case-insensitively
   * against configured keybinds. */
  guint kv_lower = gdk_keyval_to_lower(keyval);
  GdkModifierType kb_mods = state & (GDK_CONTROL_MASK |
    GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK);

  for (int i = 0; i < self->keybind_count; i++) {
    if (gdk_keyval_to_lower(self->keybinds[i].keyval) == kv_lower &&
        self->keybinds[i].mods == kb_mods) {
      switch (self->keybinds[i].action) {
        case SB_ACTION_COPY:       copy_to_clipboard(self); break;
        case SB_ACTION_PASTE:      paste_from_clipboard(self); break;
        case SB_ACTION_SELECT_ALL: sb_terminal_select_all(self); break;
        default: break;
      }
      return GDK_EVENT_STOP;
    }
  }

  /* Shift+Arrow → extend selection */
  if ((state & GDK_SHIFT_MASK) &&
      !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK))) {
    bool is_arrow = false;
    switch (keyval) {
      case GDK_KEY_Left:  case GDK_KEY_KP_Left:
      case GDK_KEY_Right: case GDK_KEY_KP_Right:
      case GDK_KEY_Up:    case GDK_KEY_KP_Up:
      case GDK_KEY_Down:  case GDK_KEY_KP_Down:
        is_arrow = true;
        break;
    }
    if (is_arrow) {
      uint16_t cx = 0, cy = 0;
      ghostty_render_state_get(self->render_state,
        GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
      ghostty_render_state_get(self->render_state,
        GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);

      if (!self->has_selection) {
        self->sel_start_col = cx;
        self->sel_start_row = cy;
        self->sel_end_col = cx;
        self->sel_end_row = cy;
        self->has_selection = true;
      }

      switch (keyval) {
        case GDK_KEY_Left:  case GDK_KEY_KP_Left:
          if (self->sel_end_col > 0) self->sel_end_col--;
          break;
        case GDK_KEY_Right: case GDK_KEY_KP_Right:
          if (self->sel_end_col < (int)self->cols - 1) self->sel_end_col++;
          break;
        case GDK_KEY_Up:    case GDK_KEY_KP_Up:
          if (self->sel_end_row > 0) self->sel_end_row--;
          break;
        case GDK_KEY_Down:  case GDK_KEY_KP_Down:
          if (self->sel_end_row < (int)self->rows - 1) self->sel_end_row++;
          break;
      }

      selection_from_cell_coords(self, self->sel_start_col, self->sel_start_row,
                                 self->sel_end_col, self->sel_end_row);
      copy_to_clipboard(self);
      gtk_widget_queue_draw(self->widget);
      return GDK_EVENT_STOP;
    }
  }

  /* Any other key when there's an active selection clears it */
  if (self->has_selection)
    selection_clear(self);

  /* Enter key → flag follow-bottom for next PTY output */
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    self->follow_bottom = true;
    if (self->follow_timer > 0)
      g_source_remove(self->follow_timer);
    self->follow_timer = g_timeout_add(2000, clear_follow, self);
  }

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
  int row_idx = 0;
  while (ghostty_render_state_row_iterator_next(self->row_iter)) {
    if (ghostty_render_state_row_get(self->row_iter,
          GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &self->cells)
        != GHOSTTY_SUCCESS) {
      row_idx++;
      continue;
    }

    int x = self->padding;
    int col_idx = 0;
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
        if (cell_is_selected(self, col_idx, row_idx)) {
          cairo_rectangle(cr, x, y, self->cell_width, self->cell_height);
          cairo_set_source_rgba(cr, 0.30, 0.55, 0.85, 0.35);
          cairo_fill(cr);
        }
        x += self->cell_width;
        col_idx++;
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

      if (cell_is_selected(self, col_idx, row_idx)) {
        cairo_rectangle(cr, x, y, self->cell_width, self->cell_height);
        cairo_set_source_rgba(cr, 0.30, 0.55, 0.85, 0.35);
        cairo_fill(cr);
      }

      /* Underline hovered URL cells */
      if (self->has_hover_url && row_idx == self->hover_url_row &&
          col_idx >= self->hover_url_start_col &&
          col_idx <= self->hover_url_end_col) {
        cairo_set_source_rgb(cr, fg.r / 255.0, fg.g / 255.0, fg.b / 255.0);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, x, y + self->cell_height - 1.5);
        cairo_line_to(cr, x + self->cell_width, y + self->cell_height - 1.5);
        cairo_stroke(cr);
      }

      x += self->cell_width;
      col_idx++;
    }

    /* Clear per-row dirty */
    bool clean = false;
    ghostty_render_state_row_set(self->row_iter,
      GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &clean);
    y += self->cell_height;
    row_idx++;
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
  gtk_widget_set_has_tooltip(self->widget, TRUE);
  g_signal_connect(self->widget, "query-tooltip",
                   G_CALLBACK(on_query_tooltip), self);
  PangoLayout *tmp = gtk_widget_create_pango_layout(self->widget, "M");
  pango_layout_set_font_description(tmp, self->font_desc);
  pango_layout_get_pixel_size(tmp, &self->cell_width, &self->cell_height);
  g_object_unref(tmp);

  /* Enforce a minimum grid of 80×24 so tools like btop always fit */
  gtk_widget_set_size_request(self->widget,
      80 * self->cell_width  + self->padding * 2,
      24 * self->cell_height + self->padding * 2);

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

  /* Set default colors (overridden by sb_terminal_apply_theme later) */
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

  /* Right-click gesture for context menu */
  self->right_click_gesture = GTK_GESTURE(gtk_gesture_click_new());
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(self->right_click_gesture),
                                GDK_BUTTON_SECONDARY);
  g_signal_connect(self->right_click_gesture, "pressed",
                   G_CALLBACK(right_click_pressed), self);
  gtk_widget_add_controller(self->widget,
                             GTK_EVENT_CONTROLLER(self->right_click_gesture));

  /* Left-click: Ctrl+click → URL, double-click → word,
     triple-click → line, Shift+click → extend selection */
  GtkGesture *left_click = GTK_GESTURE(gtk_gesture_click_new());
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(left_click),
                                GDK_BUTTON_PRIMARY);
  g_signal_connect(left_click, "pressed",
                   G_CALLBACK(left_click_pressed), self);
  g_signal_connect(left_click, "released",
                   G_CALLBACK(left_click_released), self);
  gtk_widget_add_controller(self->widget, GTK_EVENT_CONTROLLER(left_click));

  /* Middle-click to paste */
  GtkGesture *middle_click = GTK_GESTURE(gtk_gesture_click_new());
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle_click),
                                GDK_BUTTON_MIDDLE);
  g_signal_connect(middle_click, "pressed",
                   G_CALLBACK(middle_click_pressed), self);
  gtk_widget_add_controller(self->widget, GTK_EVENT_CONTROLLER(middle_click));

  /* Motion controller for URL hover and cursor tracking */
  self->motion_controller = gtk_event_controller_motion_new();
  g_signal_connect(self->motion_controller, "enter",
                   G_CALLBACK(on_motion_enter), self);
  g_signal_connect(self->motion_controller, "motion",
                   G_CALLBACK(on_motion), self);
  g_signal_connect(self->motion_controller, "leave",
                   G_CALLBACK(on_motion_leave), self);
  gtk_widget_add_controller(self->widget, self->motion_controller);

  self->context_menu = NULL;

  /* Cursor blink timer (every 500ms) */
  self->cursor_timer = g_timeout_add(500, on_cursor_tick, self);

  return self;
}

void sb_terminal_free(SbTerminal *self) {
  if (!self) return;

  if (self->cursor_timer > 0)
    g_source_remove(self->cursor_timer);
  if (self->scroll_anim_timer > 0)
    g_source_remove(self->scroll_anim_timer);
  if (self->follow_timer > 0)
    g_source_remove(self->follow_timer);

  if (self->pty_source > 0)
    g_source_remove(self->pty_source);

  if (self->pty_fd >= 0)
    close(self->pty_fd);

  if (self->child_pid > 0)
    kill(self->child_pid, SIGTERM);

  if (self->context_menu)
    gtk_widget_unparent(self->context_menu);

  g_free(self->hover_url);
  g_free(self->keybinds);

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

void sb_terminal_get_cell_size(SbTerminal *self, int *cell_width, int *cell_height) {
  if (cell_width) *cell_width = self->cell_width;
  if (cell_height) *cell_height = self->cell_height;
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

void sb_terminal_copy(SbTerminal *self) {
  copy_to_clipboard(self);
}

void sb_terminal_paste(SbTerminal *self) {
  paste_from_clipboard(self);
}

void sb_terminal_select_all(SbTerminal *self) {
  self->sel_start_col = 0;
  self->sel_start_row = 0;
  self->sel_end_col = self->cols - 1;
  self->sel_end_row = self->rows - 1;
  self->has_selection = true;
  gtk_widget_queue_draw(self->widget);
}

void sb_terminal_set_keybinds(SbTerminal *self, const SbConfigKeybind *keybinds,
                              int count) {
  g_free(self->keybinds);
  self->keybind_count = count;
  self->keybinds = g_malloc(count * sizeof(SbConfigKeybind));
  memcpy(self->keybinds, keybinds, count * sizeof(SbConfigKeybind));
}

void sb_terminal_apply_theme(SbTerminal *self, const SbTheme *theme) {
  if (!self || !theme || !self->terminal) return;

  GhosttyColorRgb fg = { theme->term_foreground.r,
                         theme->term_foreground.g,
                         theme->term_foreground.b };
  GhosttyColorRgb bg = { theme->term_background.r,
                         theme->term_background.g,
                         theme->term_background.b };
  GhosttyColorRgb cur = { theme->term_cursor.r,
                          theme->term_cursor.g,
                          theme->term_cursor.b };

  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &fg);
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &bg);
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_COLOR_CURSOR, &cur);

  /* Build a 256-color palette: first 16 from the theme, rest left at the
   * libghostty defaults by reading them back first. */
  GhosttyColorRgb palette[256];
  ghostty_terminal_get(self->terminal,
    GHOSTTY_TERMINAL_DATA_COLOR_PALETTE_DEFAULT, palette);
  for (int i = 0; i < 16; i++) {
    palette[i].r = theme->ansi[i].r;
    palette[i].g = theme->ansi[i].g;
    palette[i].b = theme->ansi[i].b;
  }
  ghostty_terminal_set(self->terminal,
    GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, palette);

  if (self->widget) gtk_widget_queue_draw(self->widget);
}
