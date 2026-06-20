/*
 * ShellBar v1.9.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#ifndef SB_WINDOW_H
#define SB_WINDOW_H

#include <adwaita.h>
#include "sb_terminal.h"

typedef struct _SbWindow SbWindow;

#define SB_TYPE_WINDOW (sb_window_get_type())
G_DECLARE_FINAL_TYPE(SbWindow, sb_window, SB, WINDOW, AdwApplicationWindow)

SbWindow *sb_window_new(AdwApplication *app);
void sb_window_reload_config(SbWindow *self);

#endif
