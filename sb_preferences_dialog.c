/*
 * ShellBar v1.1 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#include "sb_preferences_dialog.h"
#include "sb_config.h"

typedef struct {
  GtkWidget *dialog;
  GtkWidget *list;
  SbConfig *config;
  gpointer reload_target;
  void (*on_reload)(gpointer);
} PrefsData;

typedef struct {
  GtkWidget *dialog;
  PrefsData *prefs;
  SbConfigButton *target;
  GtkEntry *name_entry;
  GtkEntry *command_entry;
  GtkEntry *icon_entry;
  gboolean editing;
} EditData;

/* ------------------------------------------------------------------ */
/* List helpers                                                        */
/* ------------------------------------------------------------------ */

static GtkWidget *make_row(PrefsData *pd, SbConfigButton *btn, int idx);

static void rebuild_list(PrefsData *pd) {
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(pd->list)))
    gtk_list_box_remove(GTK_LIST_BOX(pd->list), child);
  for (int i = 0; i < pd->config->button_count; i++)
    gtk_list_box_append(GTK_LIST_BOX(pd->list),
      make_row(pd, &pd->config->buttons[i], i));
}

static void remove_button(PrefsData *pd, int idx) {
  g_free(pd->config->buttons[idx].name);
  g_free(pd->config->buttons[idx].command);
  g_free(pd->config->buttons[idx].icon);
  if (idx < pd->config->button_count - 1)
    memmove(&pd->config->buttons[idx], &pd->config->buttons[idx + 1],
            (pd->config->button_count - idx - 1) * sizeof(SbConfigButton));
  pd->config->button_count--;
  rebuild_list(pd);
}

/* ------------------------------------------------------------------ */
/* Edit / Add button dialog                                            */
/* ------------------------------------------------------------------ */

static void edit_dialog_close(EditData *ed) {
  gtk_window_destroy(GTK_WINDOW(ed->dialog));
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

  rebuild_list(ed->prefs);
  edit_dialog_close(ed);
}

static void show_edit_dialog(PrefsData *pd, SbConfigButton *target,
                             gboolean editing) {
  EditData *ed = g_malloc0(sizeof(EditData));
  ed->prefs = pd;
  ed->target = target;
  ed->editing = editing;

  GtkWidget *dlg = gtk_window_new();
  gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(pd->dialog));
  gtk_window_set_title(GTK_WINDOW(dlg), editing ? "Edit Button" : "New Button");
  gtk_window_set_default_size(GTK_WINDOW(dlg), 360, 200);
  ed->dialog = dlg;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 12);
  gtk_widget_set_margin_top(box, 12);
  gtk_widget_set_margin_bottom(box, 12);

  GtkWidget *n = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(n), "Button name");
  if (target && target->name) gtk_editable_set_text(GTK_EDITABLE(n), target->name);
  ed->name_entry = GTK_ENTRY(n);

  GtkWidget *c = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(c), "Command (e.g. pnpm build)");
  if (target && target->command) gtk_editable_set_text(GTK_EDITABLE(c), target->command);
  ed->command_entry = GTK_ENTRY(c);

  GtkWidget *i = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(i), "Icon name (GTK theme)");
  if (target && target->icon) gtk_editable_set_text(GTK_EDITABLE(i), target->icon);
  ed->icon_entry = GTK_ENTRY(i);

  gtk_box_append(GTK_BOX(box), n);
  gtk_box_append(GTK_BOX(box), c);
  gtk_box_append(GTK_BOX(box), i);

  GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(btns, GTK_ALIGN_END);
  gtk_widget_set_margin_top(btns, 8);

  GtkWidget *cancel = gtk_button_new_with_label("Cancel");
  g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(edit_dialog_close), ed);

  GtkWidget *save = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(save, "suggested-action");
  g_signal_connect(save, "clicked", G_CALLBACK(on_edit_save), ed);

  gtk_box_append(GTK_BOX(btns), cancel);
  gtk_box_append(GTK_BOX(btns), save);
  gtk_box_append(GTK_BOX(box), btns);

  gtk_window_set_child(GTK_WINDOW(dlg), box);
  gtk_window_present(GTK_WINDOW(dlg));
}

/* ------------------------------------------------------------------ */
/* Button row widget                                                    */
/* ------------------------------------------------------------------ */

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

static GtkWidget *make_row(PrefsData *pd, SbConfigButton *btn, int idx) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_start(row, 6);
  gtk_widget_set_margin_end(row, 6);
  gtk_widget_set_margin_top(row, 4);
  gtk_widget_set_margin_bottom(row, 4);

  GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  char *nm = g_markup_printf_escaped("<b>%s</b>", btn->name ? btn->name : "(no name)");
  GtkWidget *nl = gtk_label_new("");
  gtk_label_set_markup(GTK_LABEL(nl), nm);
  g_free(nm);
  gtk_widget_set_halign(nl, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(info), nl);

  GtkWidget *cl = gtk_label_new(btn->command ? btn->command : "");
  gtk_widget_add_css_class(cl, "dim-label");
  gtk_widget_set_halign(cl, GTK_ALIGN_START);
  gtk_label_set_ellipsize(GTK_LABEL(cl), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(info), cl);

  if (btn->icon && btn->icon[0]) {
    char *im = g_markup_printf_escaped("<small>icon: %s</small>", btn->icon);
    GtkWidget *il = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(il), im);
    g_free(im);
    gtk_widget_set_halign(il, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(info), il);
  }

  gtk_box_append(GTK_BOX(row), info);
  gtk_widget_set_hexpand(info, TRUE);

  GtkWidget *edit = gtk_button_new_with_label("Edit");
  g_object_set_data(G_OBJECT(edit), "pd", pd);
  g_object_set_data(G_OBJECT(edit), "idx", GINT_TO_POINTER(idx));
  g_signal_connect(edit, "clicked", G_CALLBACK(on_edit_clicked), NULL);
  gtk_box_append(GTK_BOX(row), edit);

  GtkWidget *rem = gtk_button_new_with_label("✕");
  gtk_widget_add_css_class(rem, "destructive-action");
  g_object_set_data(G_OBJECT(rem), "pd", pd);
  g_object_set_data(G_OBJECT(rem), "idx", GINT_TO_POINTER(idx));
  g_signal_connect(rem, "clicked", G_CALLBACK(on_remove_clicked), NULL);
  gtk_box_append(GTK_BOX(row), rem);

  return row;
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
  gtk_window_destroy(GTK_WINDOW(pd->dialog));
}

static void on_cancel(GtkButton *btn, gpointer userdata) {
  PrefsData *pd = userdata;
  (void)btn;
  gtk_window_destroy(GTK_WINDOW(pd->dialog));
}

static void on_add(GtkButton *btn, gpointer userdata) {
  PrefsData *pd = userdata;
  (void)btn;
  show_edit_dialog(pd, NULL, FALSE);
}

static void on_destroy(GtkWidget *widget, gpointer userdata) {
  PrefsData *pd = userdata;
  sb_config_free(pd->config);
  g_free(pd);
}

void sb_preferences_dialog_show(GtkWindow *parent, gpointer reload_target,
                                void (*on_reload)(gpointer)) {
  PrefsData *pd = g_malloc0(sizeof(PrefsData));
  pd->config = sb_config_load();
  pd->reload_target = reload_target;
  pd->on_reload = on_reload;

  GtkWidget *dlg = gtk_window_new();
  gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dlg), parent);
  gtk_window_set_title(GTK_WINDOW(dlg), "Preferences");
  gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 400);
  pd->dialog = dlg;
  g_signal_connect(dlg, "destroy", G_CALLBACK(on_destroy), pd);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  GtkWidget *sw = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(sw, TRUE);
  gtk_widget_set_margin_start(sw, 12);
  gtk_widget_set_margin_end(sw, 12);
  gtk_widget_set_margin_top(sw, 12);

  pd->list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(pd->list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class(pd->list, "rich-list");

  for (int i = 0; i < pd->config->button_count; i++)
    gtk_list_box_append(GTK_LIST_BOX(pd->list),
      make_row(pd, &pd->config->buttons[i], i));

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), pd->list);
  gtk_box_append(GTK_BOX(box), sw);

  GtkWidget *add = gtk_button_new_with_label("+ Add Button");
  gtk_widget_set_margin_start(add, 12);
  gtk_widget_set_margin_end(add, 12);
  gtk_widget_set_margin_top(add, 8);
  g_signal_connect(add, "clicked", G_CALLBACK(on_add), pd);
  gtk_box_append(GTK_BOX(box), add);

  GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(bottom, GTK_ALIGN_END);
  gtk_widget_set_margin_start(bottom, 12);
  gtk_widget_set_margin_end(bottom, 12);
  gtk_widget_set_margin_top(bottom, 12);
  gtk_widget_set_margin_bottom(bottom, 12);

  GtkWidget *cancel = gtk_button_new_with_label("Cancel");
  g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel), pd);

  GtkWidget *save = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(save, "suggested-action");
  g_signal_connect(save, "clicked", G_CALLBACK(on_save), pd);

  gtk_box_append(GTK_BOX(bottom), cancel);
  gtk_box_append(GTK_BOX(bottom), save);
  gtk_box_append(GTK_BOX(box), bottom);

  gtk_window_set_child(GTK_WINDOW(dlg), box);
  gtk_window_present(GTK_WINDOW(dlg));
}
