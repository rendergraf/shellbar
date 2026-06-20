/*
 * ShellBar v1.9.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#ifndef SB_CONFIG_H
#define SB_CONFIG_H

#include <gtk/gtk.h>

typedef enum {
  SB_ACTION_COPY,
  SB_ACTION_PASTE,
  SB_ACTION_SELECT_ALL,
  SB_ACTION_COUNT
} SbAction;

typedef struct {
  char *name;
  char *command;
  char *icon;
} SbConfigButton;

typedef struct {
  SbAction action;
  guint keyval;
  GdkModifierType mods;
} SbConfigKeybind;

typedef struct {
  SbConfigButton *buttons;
  int button_count;
  SbConfigKeybind *keybinds;
  int keybind_count;
  char *toolbar_position;
} SbConfig;

SbConfig *sb_config_load(void);
void sb_config_save(SbConfig *config);
void sb_config_free(SbConfig *config);
void sb_config_add_defaults(SbConfig *config);
const SbConfigKeybind *sb_config_find_keybind(SbConfig *config, guint keyval,
                                              GdkModifierType mods);

#endif
