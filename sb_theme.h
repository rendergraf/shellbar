/*
 * ShellBar v1.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#ifndef SB_THEME_H
#define SB_THEME_H

#include <stdint.h>
#include <stdbool.h>
#include <gtk/gtk.h>

typedef struct { uint8_t r, g, b; } SbColor;

typedef struct {
  const char *name;          /* identifier, e.g. "cyber" */
  const char *display_name;  /* human label, e.g. "Cyber" */

  /* Window chrome */
  SbColor background;        /* window/content fallback */
  SbColor surface;           /* secondary surface */
  SbColor header;            /* header/toolbar background */
  SbColor border;            /* separators */
  SbColor text_primary;
  SbColor text_secondary;

  /* Terminal */
  SbColor term_background;
  SbColor term_foreground;
  SbColor term_cursor;
  SbColor term_selection;

  /* Accents (for tabs / highlights) */
  SbColor accent_blue;
  SbColor accent_cyan;
  SbColor accent_orange;
  SbColor error;

  /* 16 ANSI palette colors (0..15). Remaining 240 are filled by libghostty. */
  SbColor ansi[16];
} SbTheme;

/* Built-in themes. Returns NULL if name unknown. */
const SbTheme *sb_theme_get(const char *name);
const SbTheme *sb_theme_default(void);
const SbTheme *const *sb_theme_list(int *out_count);

/* Apply chrome CSS for theme to the given display (idempotent: the same
 * provider is reused / replaced on subsequent calls). */
void sb_theme_apply_to_display(GdkDisplay *display, const SbTheme *theme);

#endif
