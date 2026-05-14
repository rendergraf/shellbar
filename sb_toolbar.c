/*
 * ShellBar v1.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#include "sb_toolbar.h"
#include <string.h>

struct _SbToolbar {
  GtkWidget *widget;
  GtkBox *box;
  SbTerminal *active_terminal;
};

SbToolbar *sb_toolbar_new(void) {
  SbToolbar *self = g_malloc0(sizeof(SbToolbar));

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  self->box = GTK_BOX(self->widget);

  gtk_widget_set_margin_start(self->widget, 6);
  gtk_widget_set_margin_end(self->widget, 6);
  gtk_widget_set_margin_top(self->widget, 4);
  gtk_widget_set_margin_bottom(self->widget, 4);

  gtk_widget_add_css_class(self->widget, "sb-toolbar");

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider,
    ".sb-toolbar {"
    "  background-color: rgba(30, 30, 30, 0.95);"
    "  border-top: 1px solid rgba(255, 255, 255, 0.08);"
    "}"
    ".sb-toolbar button {"
    "  background-color: rgba(255, 255, 255, 0.06);"
    "  border-radius: 4px;"
    "  border: none;"
    "  padding: 4px 10px;"
    "  color: #eaeaea;"
    "  font-size: 12px;"
    "}"
    ".sb-toolbar button:hover {"
    "  background-color: rgba(255, 255, 255, 0.12);"
    "}"
    ".sb-toolbar button:active {"
    "  background-color: rgba(255, 255, 255, 0.18);"
    "}"
  );
  gtk_style_context_add_provider_for_display(
    gtk_widget_get_display(self->widget),
    GTK_STYLE_PROVIDER(provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);

  return self;
}

static void on_button_clicked(GtkButton *button, gpointer user_data) {
  SbToolbar *self = user_data;
  const char *cmd = g_object_get_data(G_OBJECT(button), "sb-command");
  if (cmd && self->active_terminal) {
    sb_terminal_write_str(self->active_terminal, cmd);
    sb_terminal_write(self->active_terminal, "\n", 1);
  }
}

static GtkWidget *sb_toolbar_add_button(SbToolbar *self, const char *name,
                                        const char *command, const char *icon_name) {
  GtkWidget *button;
  if (icon_name && icon_name[0] != '\0')
    button = gtk_button_new_from_icon_name(icon_name);
  else
    button = gtk_button_new();
  gtk_button_set_label(GTK_BUTTON(button), name);
  gtk_widget_set_tooltip_text(button, command);
  g_object_set_data_full(G_OBJECT(button), "sb-command",
    g_strdup(command), g_free);
  g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), self);
  gtk_box_append(self->box, button);
  return button;
}

void sb_toolbar_set_buttons(SbToolbar *self, const SbToolbarButtonDef *buttons,
                            int count) {
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(self->widget)) != NULL)
    gtk_box_remove(self->box, child);

  for (int i = 0; i < count; i++)
    sb_toolbar_add_button(self, buttons[i].name,
                          buttons[i].command, buttons[i].icon_name);

  GtkWidget *add_btn = gtk_button_new_with_label("+");
  gtk_widget_set_tooltip_text(add_btn, "Add command");
  gtk_box_append(self->box, add_btn);
}

void sb_toolbar_set_active_terminal(SbToolbar *self, SbTerminal *terminal) {
  self->active_terminal = terminal;
}

GtkWidget *sb_toolbar_get_widget(SbToolbar *self) {
  return self->widget;
}

void sb_toolbar_free(SbToolbar *self) {
  if (!self) return;
  g_free(self);
}
