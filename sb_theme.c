/*
 * ShellBar v1.6.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#include "sb_theme.h"
#include <string.h>
#include <stdio.h>

#define RGB(R, G, B) { .r = 0x##R, .g = 0x##G, .b = 0x##B }

/* ------------------------------------------------------------------ */
/* Built-in themes                                                     */
/* ------------------------------------------------------------------ */

static const SbTheme cyber_theme = {
  .name = "cyber",
  .display_name = "Cyber",

  .background      = RGB(0B, 0D, 12),
  .surface         = RGB(1A, 1D, 24),
  .header          = RGB(20, 24, 2D),
  .border          = RGB(2C, 31, 3C),
  .text_primary    = RGB(E6, EA, F2),
  .text_secondary  = RGB(9A, A4, B2),

  .term_background = RGB(11, 13, 18),
  .term_foreground = RGB(E6, EA, F2),
  .term_cursor     = RGB(7E, E7, 87),
  .term_selection  = RGB(26, 4F, 78),

  .accent_blue     = RGB(4D, A3, FF),
  .accent_cyan     = RGB(56, D4, DD),
  .accent_orange   = RGB(FF, B8, 6C),
  .error           = RGB(FF, 6B, 6B),

  /* 16-color ANSI palette tuned for the Cyber theme. */
  .ansi = {
    RGB(20, 24, 2D), /* 0  black           — surface tone           */
    RGB(FF, 6B, 6B), /* 1  red             — error                  */
    RGB(7E, E7, 87), /* 2  green           — cursor green           */
    RGB(FF, B8, 6C), /* 3  yellow          — accent orange          */
    RGB(4D, A3, FF), /* 4  blue            — accent blue            */
    RGB(C7, 92, EA), /* 5  magenta                                  */
    RGB(56, D4, DD), /* 6  cyan            — accent cyan            */
    RGB(E6, EA, F2), /* 7  white           — text primary           */
    RGB(4A, 51, 60), /* 8  bright black                             */
    RGB(FF, 8E, 8E), /* 9  bright red                               */
    RGB(9D, F0, A2), /* 10 bright green                             */
    RGB(FF, CB, 8E), /* 11 bright yellow                            */
    RGB(7B, BA, FF), /* 12 bright blue                              */
    RGB(D7, B0, F1), /* 13 bright magenta                           */
    RGB(89, E1, E8), /* 14 bright cyan                              */
    RGB(F2, F5, FA), /* 15 bright white                             */
  },
};

static const SbTheme *const themes[] = {
  &cyber_theme,
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

const SbTheme *sb_theme_default(void) {
  return &cyber_theme;
}

const SbTheme *sb_theme_get(const char *name) {
  if (!name) return NULL;
  for (size_t i = 0; i < G_N_ELEMENTS(themes); i++)
    if (g_strcmp0(themes[i]->name, name) == 0) return themes[i];
  return NULL;
}

const SbTheme *const *sb_theme_list(int *out_count) {
  if (out_count) *out_count = (int)G_N_ELEMENTS(themes);
  return themes;
}

/* ------------------------------------------------------------------ */
/* CSS application                                                     */
/* ------------------------------------------------------------------ */

static GtkCssProvider *g_chrome_provider = NULL;

static void rgb_to_str(const SbColor *c, char *buf, size_t buflen) {
  snprintf(buf, buflen, "rgb(%u,%u,%u)", c->r, c->g, c->b);
}

void sb_theme_apply_to_display(GdkDisplay *display, const SbTheme *theme) {
  if (!display || !theme) return;

  if (!g_chrome_provider) {
    g_chrome_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
      display, GTK_STYLE_PROVIDER(g_chrome_provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);
  }

  char bg[24], surface[24], header[24], border[24];
  char text_pri[24], text_sec[24];
  char accent_blue[24], accent_cyan[24], accent_orange[24];
  char term_bg[24], term_sel[24];
  rgb_to_str(&theme->background,      bg,            sizeof(bg));
  rgb_to_str(&theme->surface,         surface,       sizeof(surface));
  rgb_to_str(&theme->header,          header,        sizeof(header));
  rgb_to_str(&theme->border,          border,        sizeof(border));
  rgb_to_str(&theme->text_primary,    text_pri,      sizeof(text_pri));
  rgb_to_str(&theme->text_secondary,  text_sec,      sizeof(text_sec));
  rgb_to_str(&theme->accent_blue,     accent_blue,   sizeof(accent_blue));
  rgb_to_str(&theme->accent_cyan,     accent_cyan,   sizeof(accent_cyan));
  rgb_to_str(&theme->accent_orange,   accent_orange, sizeof(accent_orange));
  rgb_to_str(&theme->term_background, term_bg,       sizeof(term_bg));
  rgb_to_str(&theme->term_selection,  term_sel,      sizeof(term_sel));

  char *css = g_strdup_printf(
    /* Window + Adwaita named-color overrides */
    "window { background-color: %s; color: %s; }"
    ":root, window {"
    "  --window-bg-color: %s;"
    "  --window-fg-color: %s;"
    "  --view-bg-color: %s;"
    "  --headerbar-bg-color: %s;"
    "  --headerbar-fg-color: %s;"
    "  --accent-bg-color: %s;"
    "  --accent-fg-color: %s;"
    "}"
    /* Bottom toolbar/header band */
    ".sb-chrome {"
    "  background-color: %s;"
    "  color: %s;"
    "  border-top: 1px solid %s;"
    "}"
    /* Generic surfaces inside the chrome */
    ".sb-chrome button {"
    "  color: %s;"
    "}"
    ".sb-chrome button:hover {"
    "  background-color: alpha(%s, 0.08);"
    "}"
    /* Toolbar (command buttons bar) */
    ".sb-toolbar {"
    "  background-color: alpha(%s, 0.08);"
    "  border-top: 1px solid alpha(%s, 0.06);"
    "  border-radius: 4px;"
    "}"
    ".sb-toolbar button {"
    "  background-color: alpha(%s, 0.06);"
    "  border-radius: 4px;"
    "  border: none;"
    "  padding: 4px 10px;"
    "  color: %s;"
    "  font-size: 10px;"
    "}"
    ".sb-toolbar button:hover {"
    "  background-color: alpha(%s, 0.4);"
    "}"
    ".sb-toolbar button:active {"
    "  background-color: alpha(%s, 0.01);"
    "}"
    /* Tab pill colors driven by the theme */
    ".sb-tab-row { background: alpha(%s, 0.10); border-radius: 6px;"
    "  padding: 0; margin: 0 2px; }"
    ".sb-tab-row:hover { background: alpha(%s, 0.16); }"
    ".sb-tab-row.active { background: alpha(%s, 0.22); }"
    ".sb-tab-row.active:hover { background: alpha(%s, 0.28); }"
    ".sb-tab-button { border-radius: 5px 0 0 5px; padding: 2px 8px 2px 10px;"
    "  min-height: 24px; background: transparent; box-shadow: none;"
    "  border: none; outline: none; color: %s; }"
    ".sb-tab-button:hover { background: transparent; color: %s; }"
    ".sb-tab-button:checked { background: transparent; color: %s;"
    "  font-weight: bold; box-shadow: none; border: none; }"
    ".sb-tab-button:checked:hover { background: transparent; }"
    ".sb-tab-button:focus, .sb-tab-button:focus-visible {"
    "  outline: none; box-shadow: none; border: none; }"
    ".sb-tab-button > check, .sb-tab-button > indicator {"
    "  background: none; min-width: 0; min-height: 0; padding: 0;"
    "  margin: 0; border: none; }"
    ".sb-tab-close { min-width: 18px; min-height: 18px; padding: 0;"
    "  margin: 0 4px 0 0; border-radius: 9999px; -gtk-icon-size: 12px;"
    "  background: transparent; color: alpha(%s, 0.65); }"
    ".sb-tab-close:hover { background: transparent; color: %s; }"
    ".sb-tab-row.active .sb-tab-close { color: %s; }",
    /* window */
    bg, text_pri,
    /* :root vars */
    bg, text_pri, surface, header, text_pri, accent_blue, text_pri,
    /* .sb-chrome */
    header, text_pri, border,
    /* .sb-chrome button */
    text_pri,
    /* .sb-chrome button:hover bg uses text_pri base */
    text_pri,
    /* .sb-toolbar */
    accent_blue,
    text_pri,
    /* .sb-toolbar button */
    text_pri, text_pri,
    /* .sb-toolbar button:hover/active */
    accent_blue, accent_blue,
    /* .sb-tab-row */
    text_sec, text_sec, accent_blue, accent_blue,
    /* .sb-tab-button */
    text_sec, text_pri, text_pri,
    /* .sb-tab-close */
    text_pri, text_pri, text_pri
  );

  gtk_css_provider_load_from_string(g_chrome_provider, css);
  g_free(css);

  /* Avoid unused-variable warnings when only some fields are referenced. */
  (void)accent_cyan; (void)accent_orange; (void)term_bg; (void)term_sel;
}
