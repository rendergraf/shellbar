/*
 * ShellBar v1.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#ifndef SB_TERMINAL_H
#define SB_TERMINAL_H

#include <gtk/gtk.h>
#include "sb_config.h"

typedef struct _SbTerminal SbTerminal;

typedef void (*SbTerminalTitleCb)(SbTerminal *terminal, const char *title, void *userdata);

SbTerminal *sb_terminal_new(void);
void sb_terminal_free(SbTerminal *terminal);
GtkWidget *sb_terminal_get_widget(SbTerminal *terminal);
int sb_terminal_get_pty_fd(SbTerminal *terminal);
void sb_terminal_write(SbTerminal *terminal, const char *data, size_t len);
void sb_terminal_write_str(SbTerminal *terminal, const char *str);
void sb_terminal_set_title_callback(SbTerminal *terminal, SbTerminalTitleCb cb, void *userdata);
gboolean sb_terminal_handle_key(SbTerminal *terminal, guint keyval, guint keycode, GdkModifierType state);

void sb_terminal_copy(SbTerminal *self);
void sb_terminal_paste(SbTerminal *self);
void sb_terminal_select_all(SbTerminal *self);
void sb_terminal_set_keybinds(SbTerminal *self, const SbConfigKeybind *keybinds, int count);

#endif
