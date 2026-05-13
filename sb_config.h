/*
 * ShellBar v1.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#ifndef SB_CONFIG_H
#define SB_CONFIG_H

#include <gtk/gtk.h>

typedef struct {
  char *name;
  char *command;
  char *icon;
} SbConfigButton;

typedef struct {
  SbConfigButton *buttons;
  int button_count;
} SbConfig;

SbConfig *sb_config_load(void);
void sb_config_save(SbConfig *config);
void sb_config_free(SbConfig *config);
void sb_config_add_defaults(SbConfig *config);

#endif
