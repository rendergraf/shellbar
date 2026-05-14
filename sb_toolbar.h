/*
 * ShellBar v1.1 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#ifndef SB_TOOLBAR_H
#define SB_TOOLBAR_H

#include <gtk/gtk.h>
#include "sb_terminal.h"

typedef struct _SbToolbar SbToolbar;

typedef struct {
  const char *name;
  const char *command;
  const char *icon_name;
} SbToolbarButtonDef;

SbToolbar *sb_toolbar_new(void);
void sb_toolbar_free(SbToolbar *toolbar);
GtkWidget *sb_toolbar_get_widget(SbToolbar *toolbar);
void sb_toolbar_set_buttons(SbToolbar *toolbar, const SbToolbarButtonDef *buttons, int count);
void sb_toolbar_set_active_terminal(SbToolbar *toolbar, SbTerminal *terminal);

#endif
