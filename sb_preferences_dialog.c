/*
 * ShellBar v1.7.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#include "sb_preferences_dialog.h"
#include "sb_config.h"

typedef struct {
  GtkWidget *window;
  GtkWidget *group;
  SbConfig *config;
  gpointer reload_target;
  void (*on_reload)(gpointer);
  void (*on_close)(gpointer);
  gpointer close_data;
} PrefsData;

typedef struct {
  GtkWidget *window;
  PrefsData *prefs;
  SbConfigButton *target;
  GtkWidget *name_entry;
  GtkWidget *command_entry;
  GtkWidget *icon_entry;
  gboolean editing;
} EditData;

/* ------------------------------------------------------------------ */
/* List helpers                                                        */
/* ------------------------------------------------------------------ */

static void rebuild_group(PrefsData *pd);

static void remove_button(PrefsData *pd, int idx) {
  g_free(pd->config->buttons[idx].name);
  g_free(pd->config->buttons[idx].command);
  g_free(pd->config->buttons[idx].icon);
  if (idx < pd->config->button_count - 1)
    memmove(&pd->config->buttons[idx], &pd->config->buttons[idx + 1],
            (pd->config->button_count - idx - 1) * sizeof(SbConfigButton));
  pd->config->button_count--;
  rebuild_group(pd);
}

/* ------------------------------------------------------------------ */
/* Edit button dialog                                                  */
/* ------------------------------------------------------------------ */

static void edit_dialog_close(EditData *ed) {
  gtk_window_destroy(GTK_WINDOW(ed->window));
  g_free(ed);
}

static void on_edit_save(GtkButton *btn, gpointer userdata) {
  EditData *ed = userdata;
  (void)btn;

  const char *name = gtk_editable_get_text(GTK_EDITABLE(ed->name_entry));
  const char *cmd  = gtk_editable_get_text(GTK_EDITABLE(ed->command_entry));
  const char *icon = gtk_editable_get_text(GTK_EDITABLE(ed->icon_entry));

  if (!name || !name[0] || !cmd || !cmd[0]) return;

  if (ed->editing) {
    g_free(ed->target->name);
    g_free(ed->target->command);
    g_free(ed->target->icon);
  } else {
    ed->prefs->config->button_count++;
    ed->prefs->config->buttons = g_realloc(ed->prefs->config->buttons,
      ed->prefs->config->button_count * sizeof(SbConfigButton));
    ed->target = &ed->prefs->config->buttons[ed->prefs->config->button_count - 1];
  }

  ed->target->name    = g_strdup(name);
  ed->target->command = g_strdup(cmd);
  ed->target->icon    = g_strdup(icon && icon[0] ? icon : "");

  rebuild_group(ed->prefs);
  edit_dialog_close(ed);
}

static void show_edit_dialog(PrefsData *pd, SbConfigButton *target,
                             gboolean editing) {
  EditData *ed = g_malloc0(sizeof(EditData));
  ed->prefs = pd;
  ed->target = target;
  ed->editing = editing;

  GtkWidget *win = adw_window_new();
  gtk_window_set_modal(GTK_WINDOW(win), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(pd->window));
  gtk_window_set_title(GTK_WINDOW(win), editing ? "Edit Button" : "New Button");
  gtk_window_set_default_size(GTK_WINDOW(win), 400, 360);
  ed->window = win;

  AdwToolbarView *tview = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());

  GtkWidget *header = adw_header_bar_new();
  adw_toolbar_view_add_top_bar(tview, header);

  GtkWidget *cancel = gtk_button_new_with_label("Cancel");
  g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(edit_dialog_close), ed);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), cancel);

  GtkWidget *save = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(save, "suggested-action");
  g_signal_connect(save, "clicked", G_CALLBACK(on_edit_save), ed);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), save);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start(box, 24);
  gtk_widget_set_margin_end(box, 24);
  gtk_widget_set_margin_top(box, 24);
  gtk_widget_set_margin_bottom(box, 24);

  GtkWidget *nl = gtk_label_new("Name");
  gtk_widget_add_css_class(nl, "heading");
  gtk_widget_set_halign(nl, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), nl);

  GtkWidget *n = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(n), "e.g. Build APP");
  if (target && target->name) gtk_editable_set_text(GTK_EDITABLE(n), target->name);
  gtk_box_append(GTK_BOX(box), n);
  ed->name_entry = n;

  GtkWidget *cl = gtk_label_new("Command");
  gtk_widget_add_css_class(cl, "heading");
  gtk_widget_set_halign(cl, GTK_ALIGN_START);
  gtk_widget_set_margin_top(cl, 8);
  gtk_box_append(GTK_BOX(box), cl);

  GtkWidget *c = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(c), "e.g. pnpm build");
  if (target && target->command) gtk_editable_set_text(GTK_EDITABLE(c), target->command);
  gtk_box_append(GTK_BOX(box), c);
  ed->command_entry = c;

  GtkWidget *il = gtk_label_new("Icon (optional)");
  gtk_widget_add_css_class(il, "heading");
  gtk_widget_set_halign(il, GTK_ALIGN_START);
  gtk_widget_set_margin_top(il, 8);
  gtk_box_append(GTK_BOX(box), il);

  GtkWidget *i = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(i), "e.g. media-playback-start");
  if (target && target->icon) gtk_editable_set_text(GTK_EDITABLE(i), target->icon);
  gtk_box_append(GTK_BOX(box), i);
  ed->icon_entry = i;

  adw_toolbar_view_set_content(tview, box);
  adw_window_set_content(ADW_WINDOW(win), GTK_WIDGET(tview));
  gtk_window_present(GTK_WINDOW(win));
}

/* ------------------------------------------------------------------ */
/* Button row widget                                                    */
/* ------------------------------------------------------------------ */

static void move_button(PrefsData *pd, int from, int to) {
  if (from == to || from < 0 || to < 0 ||
      from >= pd->config->button_count || to >= pd->config->button_count)
    return;

  SbConfigButton moved = pd->config->buttons[from];
  if (from < to)
    memmove(&pd->config->buttons[from], &pd->config->buttons[from + 1],
            (to - from) * sizeof(SbConfigButton));
  else
    memmove(&pd->config->buttons[to + 1], &pd->config->buttons[to],
            (from - to) * sizeof(SbConfigButton));
  pd->config->buttons[to] = moved;
  rebuild_group(pd);
}

static GtkWidget *drop_highlight_row = NULL;

static void clear_drop_highlight(void) {
  if (drop_highlight_row) {
    if (GTK_IS_WIDGET(drop_highlight_row))
      gtk_widget_remove_css_class(drop_highlight_row, "drop-highlight");
    drop_highlight_row = NULL;
  }
}

typedef struct {
  PrefsData *pd;
  int from;
  int to;
} DeferredMove;

static gboolean do_deferred_move(gpointer data) {
  DeferredMove *dm = data;
  move_button(dm->pd, dm->from, dm->to);
  g_free(dm);
  return G_SOURCE_REMOVE;
}

static GdkContentProvider *on_drag_prepare(GtkDragSource *source,
                                             double x, double y,
                                             gpointer user_data) {
  (void)source;
  (void)x;
  (void)y;
  int idx = GPOINTER_TO_INT(user_data);
  return gdk_content_provider_new_typed(G_TYPE_INT, idx);
}

static void on_drag_begin(GtkDragSource *source, GdkDrag *drag,
                           gpointer user_data) {
  (void)source;
  (void)drag;
  GtkWidget *grip = user_data;
  if (!GTK_IS_WIDGET(grip)) return;
  GtkWidget *row = gtk_widget_get_parent(grip);
  if (GTK_IS_WIDGET(row)) {
    gtk_widget_set_opacity(row, 0.4);
    gtk_widget_set_cursor_from_name(grip, "grabbing");
  }
}

static void on_drag_end(GtkDragSource *source, GdkDrag *drag,
                         gboolean deleted, gpointer user_data) {
  (void)source;
  (void)drag;
  (void)deleted;
  GtkWidget *grip = user_data;
  if (!GTK_IS_WIDGET(grip)) return;
  GtkWidget *row = gtk_widget_get_parent(grip);
  if (GTK_IS_WIDGET(row))
    gtk_widget_set_opacity(row, 1.0);
  gtk_widget_set_cursor_from_name(grip, "grab");
}

static GdkDragAction on_drop_motion(GtkDropTarget *target, gdouble x,
                                     gdouble y, gpointer user_data) {
  (void)x;
  PrefsData *pd = user_data;
  GtkWidget *target_child = NULL;

  GtkWidget *child;
  for (child = gtk_widget_get_first_child(pd->group); child;
       child = gtk_widget_get_next_sibling(child)) {
    double h = gtk_widget_get_height(child);
    if (y < h / 2) { target_child = child; break; }
    y -= h;
  }
  if (!target_child)
    target_child = gtk_widget_get_last_child(pd->group);

  if (target_child != drop_highlight_row) {
    if (drop_highlight_row && GTK_IS_WIDGET(drop_highlight_row))
      gtk_widget_remove_css_class(drop_highlight_row, "drop-highlight");
    drop_highlight_row = target_child;
    if (target_child)
      gtk_widget_add_css_class(target_child, "drop-highlight");
  }

  return GDK_ACTION_MOVE;
}

static void on_drop_leave(GtkDropTarget *target, gpointer user_data) {
  (void)target;
  (void)user_data;
  clear_drop_highlight();
}

static gboolean on_drop(GtkDropTarget *target, const GValue *value,
                        double x, double y, gpointer user_data) {
  (void)x;
  PrefsData *pd = user_data;
  int src = g_value_get_int(value);

  clear_drop_highlight();

  GtkWidget *child;
  int dest = 0;
  for (child = gtk_widget_get_first_child(pd->group); child;
       child = gtk_widget_get_next_sibling(child), dest++) {
    double h = gtk_widget_get_height(child);
    if (y < h / 2) break;
    y -= h;
  }
  if (dest > src) dest--;

  DeferredMove *dm = g_new(DeferredMove, 1);
  dm->pd = pd;
  dm->from = src;
  dm->to = dest;
  g_idle_add(do_deferred_move, dm);
  return TRUE;
}

static void on_edit_clicked(GtkButton *btn, gpointer userdata) {
  PrefsData *pd = g_object_get_data(G_OBJECT(btn), "pd");
  int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "idx"));
  if (idx >= 0 && idx < pd->config->button_count)
    show_edit_dialog(pd, &pd->config->buttons[idx], TRUE);
}

static void on_remove_clicked(GtkButton *btn, gpointer userdata) {
  PrefsData *pd = g_object_get_data(G_OBJECT(btn), "pd");
  int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "idx"));
  if (idx >= 0 && idx < pd->config->button_count)
    remove_button(pd, idx);
}

static const char *drag_handle_icon_name(GtkWidget *context_widget) {
  (void)context_widget;
  return "/com/shellbar/icons/scalable/actions/drag-handle.svg";
}

static void add_row_to_group(PrefsData *pd, SbConfigButton *btn, int idx) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class(row, "card");
  gtk_widget_set_margin_start(row, 12);
  gtk_widget_set_margin_end(row, 12);
  gtk_widget_set_margin_top(row, 2);
  gtk_widget_set_margin_bottom(row, 2);
  g_object_set_data(G_OBJECT(row), "idx", GINT_TO_POINTER(idx));

  const char *icon_path = drag_handle_icon_name(pd->group);
  GtkWidget *grip = gtk_image_new_from_resource(icon_path);
  gtk_image_set_pixel_size(GTK_IMAGE(grip), 20);
  gtk_widget_set_size_request(grip, 24, 24);
  gtk_widget_set_valign(grip, GTK_ALIGN_CENTER);
  gtk_widget_set_cursor_from_name(grip, "grab");
  gtk_widget_set_margin_start(grip, 2);
  gtk_widget_set_margin_end(grip, 6);
  gtk_widget_add_css_class(grip, "drag-handle");
  gtk_box_append(GTK_BOX(row), grip);

  GtkDragSource *drag = gtk_drag_source_new();
  gtk_drag_source_set_actions(drag, GDK_ACTION_MOVE);
  g_signal_connect(drag, "prepare", G_CALLBACK(on_drag_prepare),
                   GINT_TO_POINTER(idx));
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), grip);
  g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), grip);
  gtk_widget_add_controller(grip, GTK_EVENT_CONTROLLER(drag));

  GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand(info, TRUE);
  gtk_widget_set_margin_start(info, 6);
  gtk_widget_set_margin_end(info, 6);
  gtk_widget_set_margin_top(info, 4);
  gtk_widget_set_margin_bottom(info, 4);

  GtkWidget *tl = gtk_label_new(btn->name ? btn->name : "(no name)");
  gtk_widget_add_css_class(tl, "body");
  gtk_label_set_ellipsize(GTK_LABEL(tl), PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign(tl, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(info), tl);

  if (btn->command) {
    GtkWidget *sl = gtk_label_new(btn->command);
    gtk_widget_add_css_class(sl, "dim-label");
    gtk_widget_add_css_class(sl, "caption");
    gtk_label_set_ellipsize(GTK_LABEL(sl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(sl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(info), sl);
  }

  gtk_box_append(GTK_BOX(row), info);

  GtkWidget *edit = gtk_button_new_from_icon_name("document-edit-symbolic");
  gtk_widget_add_css_class(edit, "flat");
  gtk_widget_set_valign(edit, GTK_ALIGN_CENTER);
  g_object_set_data(G_OBJECT(edit), "pd", pd);
  g_object_set_data(G_OBJECT(edit), "idx", GINT_TO_POINTER(idx));
  g_signal_connect(edit, "clicked", G_CALLBACK(on_edit_clicked), NULL);
  gtk_box_append(GTK_BOX(row), edit);

  GtkWidget *rem = gtk_button_new_from_icon_name("user-trash-symbolic");
  gtk_widget_add_css_class(rem, "flat");
  gtk_widget_set_valign(rem, GTK_ALIGN_CENTER);
  g_object_set_data(G_OBJECT(rem), "pd", pd);
  g_object_set_data(G_OBJECT(rem), "idx", GINT_TO_POINTER(idx));
  g_signal_connect(rem, "clicked", G_CALLBACK(on_remove_clicked), NULL);
  gtk_box_append(GTK_BOX(row), rem);

  gtk_box_append(GTK_BOX(pd->group), row);
}

static void rebuild_group(PrefsData *pd) {
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(pd->group)))
    gtk_box_remove(GTK_BOX(pd->group), child);

  for (int i = 0; i < pd->config->button_count; i++)
    add_row_to_group(pd, &pd->config->buttons[i], i);
}

/* ------------------------------------------------------------------ */
/* Main dialog                                                         */
/* ------------------------------------------------------------------ */

static void on_save(GtkButton *btn, gpointer userdata) {
  PrefsData *pd = userdata;
  (void)btn;
  sb_config_save(pd->config);
  if (pd->on_reload)
    pd->on_reload(pd->reload_target);
  gtk_window_destroy(GTK_WINDOW(pd->window));
}

static void on_add(GtkButton *btn, gpointer userdata) {
  PrefsData *pd = userdata;
  (void)btn;
  show_edit_dialog(pd, NULL, FALSE);
}

static void on_destroy(GtkWidget *widget, gpointer user_data) {
  (void)widget;
  PrefsData *pd = user_data;
  sb_config_free(pd->config);
  if (pd->on_close)
    pd->on_close(pd->close_data);
  g_free(pd);
}

void sb_preferences_dialog_show(GtkWindow *parent, gpointer reload_target,
                                void (*on_reload)(gpointer),
                                void (*on_close)(gpointer),
                                gpointer close_data) {
  PrefsData *pd = g_malloc0(sizeof(PrefsData));
  pd->config = sb_config_load();
  pd->reload_target = reload_target;
  pd->on_reload = on_reload;
  pd->on_close = on_close;
  pd->close_data = close_data;

  GtkWidget *win = adw_window_new();
  gtk_window_set_modal(GTK_WINDOW(win), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(win), parent);
  gtk_window_set_title(GTK_WINDOW(win), "Preferences");
  gtk_window_set_default_size(GTK_WINDOW(win), 520, 480);
  pd->window = win;
  g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), pd);

  AdwToolbarView *tview = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());

  /* Header bar */
  GtkWidget *header = adw_header_bar_new();
  adw_toolbar_view_add_top_bar(tview, header);

  GtkWidget *cancel = gtk_button_new_with_label("Cancel");
  g_signal_connect_swapped(cancel, "clicked",
    G_CALLBACK(gtk_window_destroy), win);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), cancel);

  GtkWidget *add_btn = gtk_button_new_from_icon_name("list-add-symbolic");
  gtk_widget_set_tooltip_text(add_btn, "Add command button");
  gtk_widget_add_css_class(add_btn, "flat");
  g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add), pd);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), add_btn);

  GtkWidget *save_btn = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(save_btn, "suggested-action");
  g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save), pd);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), save_btn);

  /* Content */
  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  GtkWidget *desc = gtk_label_new(
    "Custom shortcuts that appear in the bottom toolbar. "
    "Press Alt+1..0 to trigger them.");
  gtk_widget_add_css_class(desc, "dim-label");
  gtk_widget_add_css_class(desc, "caption");
  gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
  gtk_widget_set_margin_start(desc, 12);
  gtk_widget_set_margin_end(desc, 12);
  gtk_widget_set_margin_top(desc, 8);
  gtk_widget_set_margin_bottom(desc, 8);
  gtk_box_append(GTK_BOX(content), desc);

  GtkWidget *sw = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(sw, TRUE);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_widget_set_margin_start(sw, 8);
  gtk_widget_set_margin_end(sw, 8);
  gtk_box_append(GTK_BOX(content), sw);

  GtkWidget *group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  pd->group = group;

  GtkDropTarget *drop = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_MOVE);
  g_signal_connect(drop, "drop", G_CALLBACK(on_drop), pd);
  g_signal_connect(drop, "motion", G_CALLBACK(on_drop_motion), pd);
  g_signal_connect(drop, "leave", G_CALLBACK(on_drop_leave), pd);
  gtk_widget_add_controller(group, GTK_EVENT_CONTROLLER(drop));

  /* CSS for drag feedback */
  GtkCssProvider *drag_css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(drag_css,
    ".card { border-radius: 0; }"
    ".drag-handle { color: @text_color; opacity: 0.8; -gtk-icon-size: 20px; }"
    ".drop-highlight { border-top: 2px solid @accent_color; }");
  gtk_style_context_add_provider_for_display(
    gtk_widget_get_display(group),
    GTK_STYLE_PROVIDER(drag_css),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(drag_css);

  for (int i = 0; i < pd->config->button_count; i++)
    add_row_to_group(pd, &pd->config->buttons[i], i);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), group);

  adw_toolbar_view_set_content(tview, content);
  adw_window_set_content(ADW_WINDOW(win), GTK_WIDGET(tview));
  gtk_window_present(GTK_WINDOW(win));
}
