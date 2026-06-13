/*
 * ShellBar v1.8.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#ifndef SB_PREFERENCES_DIALOG_H
#define SB_PREFERENCES_DIALOG_H

#include <adwaita.h>

void sb_preferences_dialog_show(GtkWindow *parent, gpointer reload_target,
                                void (*on_reload)(gpointer),
                                void (*on_close)(gpointer),
                                gpointer close_data);

#endif
