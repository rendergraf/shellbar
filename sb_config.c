/*
 * ShellBar v1.6.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#include "sb_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
  const char *name;
  const char *command;
  const char *icon;
} SbDefaultButton;

static const SbDefaultButton default_buttons[] = {
  { "Storybook", "pnpm storybook", "media-playback-start" },
  { "Build",     "pnpm build",     "emblem-system" },
  { "Test",      "pnpm test",      "emblem-default" },
  { "Dev",       "pnpm dev",       "computer" },
  { "Lint",      "pnpm lint",      "emblem-important" },
};
static const int default_count = sizeof(default_buttons) / sizeof(default_buttons[0]);

static const SbConfigKeybind default_keybinds[] = {
  { SB_ACTION_COPY,       GDK_KEY_c, GDK_CONTROL_MASK | GDK_SHIFT_MASK },
  { SB_ACTION_PASTE,      GDK_KEY_v, GDK_CONTROL_MASK | GDK_SHIFT_MASK },
  { SB_ACTION_SELECT_ALL, GDK_KEY_a, GDK_CONTROL_MASK | GDK_SHIFT_MASK },
};
static const int default_kb_count = sizeof(default_keybinds) / sizeof(default_keybinds[0]);

void sb_config_add_defaults(SbConfig *config) {
  if (config->button_count == 0) {
    for (int i = 0; i < default_count; i++) {
      config->button_count++;
      config->buttons = g_realloc(config->buttons,
        config->button_count * sizeof(SbConfigButton));
      SbConfigButton *b = &config->buttons[config->button_count - 1];
      b->name    = g_strdup(default_buttons[i].name);
      b->command = g_strdup(default_buttons[i].command);
      b->icon    = g_strdup(default_buttons[i].icon);
    }
  }
  if (config->keybind_count == 0) {
    for (int i = 0; i < default_kb_count; i++) {
      config->keybind_count++;
      config->keybinds = g_realloc(config->keybinds,
        config->keybind_count * sizeof(SbConfigKeybind));
      config->keybinds[config->keybind_count - 1] = default_keybinds[i];
    }
  }
}

/* ------------------------------------------------------------------ */
/* Config path resolution                                              */
/* ------------------------------------------------------------------ */

static char *config_path(void) {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0])
    return g_build_filename(xdg, "shellbar", "config", NULL);
  const char *home = getenv("HOME");
  if (home)
    return g_build_filename(home, ".config", "shellbar", "config", NULL);
  return NULL;
}

/* ------------------------------------------------------------------ */
/* Line parser                                                         */
/* ------------------------------------------------------------------ */

static char *trim_start(char *s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

static void trim_end(char *s) {
  size_t len = strlen(s);
  while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                     s[len-1] == '\r' || s[len-1] == '\n'))
    s[--len] = '\0';
}

/* Parse a quoted string value like "foo" or "foo bar".
   Advances *pp to after the closing quote. Returns allocated string. */
static char *parse_quoted(char **pp) {
  char *p = *pp;
  p = trim_start(p);
  if (*p != '"') return NULL;
  p++; /* skip opening quote */
  size_t cap = 64, len = 0;
  char *val = g_malloc(cap);
  for (; *p && *p != '"'; p++) {
    if (*p == '\\' && *(p+1)) { p++; } /* skip escape */
    if (len + 1 >= cap) { cap *= 2; val = g_realloc(val, cap); }
    val[len++] = *p;
  }
  if (*p == '"') p++; /* skip closing quote */
  val[len] = '\0';
  *pp = p;
  return val;
}

/* Parse an unquoted value until comma or end. Returns allocated string. */
static char *parse_unquoted(char **pp) {
  char *p = *pp;
  p = trim_start(p);
  size_t cap = 64, len = 0;
  char *val = g_malloc(cap);
  for (; *p && *p != ','; p++) {
    if (len + 1 >= cap) { cap *= 2; val = g_realloc(val, cap); }
    val[len++] = *p;
  }
  val[len] = '\0';
  trim_end(val);
  *pp = p;
  return val;
}

/* Parse a toolbar-button value like:
   name="Storybook", command="pnpm storybook\n", icon="media-playback-start" */
static SbConfigButton parse_button_value(const char *value) {
  SbConfigButton btn = { NULL, NULL, NULL };
  char *buf = g_strdup(value);
  char *p = buf;
  while (*p) {
    p = trim_start(p);
    if (!*p) break;
    /* Read key */
    char *key_start = p;
    while (*p && *p != '=' && *p != ',' && *p != ' ') p++;
    char saved = *p;
    *p = '\0';
    char *key = g_strdup(key_start);
    trim_end(key);
    *p = saved;
    if (*p == '=') p++;
    p = trim_start(p);
    /* Read value */
    char *val = NULL;
    if (*p == '"') {
      val = parse_quoted(&p);
    } else {
      val = parse_unquoted(&p);
    }
    if (val) {
      if (strcmp(key, "name") == 0) btn.name = val;
      else if (strcmp(key, "command") == 0) btn.command = val;
      else if (strcmp(key, "icon") == 0) btn.icon = val;
      else g_free(val);
    }
    g_free(key);
    if (*p == ',') p++;
  }
  g_free(buf);
  return btn;
}

/* ------------------------------------------------------------------ */
/* Keybind helpers                                                      */
/* ------------------------------------------------------------------ */

static SbAction action_from_name(const char *name) {
  if (strcmp(name, "copy") == 0)       return SB_ACTION_COPY;
  if (strcmp(name, "paste") == 0)      return SB_ACTION_PASTE;
  if (strcmp(name, "select_all") == 0) return SB_ACTION_SELECT_ALL;
  return SB_ACTION_COUNT;
}

static GdkModifierType parse_mods(const char *str) {
  GdkModifierType mods = 0;
  char *buf = g_strdup(str);
  char *tok = strtok(buf, "+");
  while (tok) {
    char *t = trim_start(tok);
    trim_end(t);
    if (strcmp(t, "ctrl") == 0 || strcmp(t, "control") == 0)
      mods |= GDK_CONTROL_MASK;
    else if (strcmp(t, "shift") == 0)
      mods |= GDK_SHIFT_MASK;
    else if (strcmp(t, "alt") == 0)
      mods |= GDK_ALT_MASK;
    else if (strcmp(t, "super") == 0)
      mods |= GDK_SUPER_MASK;
    tok = strtok(NULL, "+");
  }
  g_free(buf);
  return mods;
}

static SbConfigKeybind parse_keybind_value(const char *value) {
  SbConfigKeybind kb = { SB_ACTION_COUNT, 0, 0 };
  char *act = NULL, *kname = NULL, *mstr = NULL;
  char *buf = g_strdup(value);
  char *p = buf;
  while (*p) {
    p = trim_start(p);
    if (!*p) break;
    char *key_start = p;
    while (*p && *p != '=' && *p != ',' && *p != ' ') p++;
    char saved = *p;
    *p = '\0';
    char *key = g_strdup(key_start);
    trim_end(key);
    *p = saved;
    if (*p == '=') p++;
    p = trim_start(p);
    char *val = NULL;
    if (*p == '"') {
      val = parse_quoted(&p);
    } else {
      val = parse_unquoted(&p);
    }
    if (val) {
      if (strcmp(key, "action") == 0)      act = val;
      else if (strcmp(key, "key") == 0)    kname = val;
      else if (strcmp(key, "mods") == 0)   mstr = val;
      else g_free(val);
    }
    g_free(key);
    if (*p == ',') p++;
  }
  if (act) {
    kb.action = action_from_name(act);
    g_free(act);
  }
  if (kname) {
    kb.keyval = gdk_keyval_from_name(kname);
    if (kb.keyval == GDK_KEY_VoidSymbol && kname[0] && !kname[1])
      kb.keyval = gdk_unicode_to_keyval((gunichar)kname[0]);
    g_free(kname);
  }
  if (mstr) {
    kb.mods = parse_mods(mstr);
    g_free(mstr);
  }
  g_free(buf);
  return kb;
}

/* ------------------------------------------------------------------ */
/* Config loading                                                      */
/* ------------------------------------------------------------------ */

SbConfig *sb_config_load(void) {
  SbConfig *config = g_malloc0(sizeof(SbConfig));

  char *path = config_path();
  if (!path) { sb_config_add_defaults(config); return config; }

  FILE *f = fopen(path, "r");
  if (!f) { sb_config_add_defaults(config); g_free(path); return config; }
  g_free(path);

  char line[4096];
  int line_no = 0;
  while (fgets(line, sizeof(line), f)) {
    line_no++;
    char *p = trim_start(line);
    if (!*p || *p == '#') continue;
    trim_end(p);

    char *eq = strchr(p, '=');
    if (!eq) continue;

    *eq = '\0';
    char *key = p;
    trim_end(key);
    char *value = trim_start(eq + 1);

    if (strcmp(key, "toolbar-button") == 0) {
      SbConfigButton btn = parse_button_value(value);
      g_printerr("[shellbar] line %d: name='%s' cmd='%s'\n",
                 line_no,
                 btn.name ? btn.name : "NULL",
                 btn.command ? btn.command : "NULL");
      if (btn.name && btn.command) {
        config->button_count++;
        config->buttons = g_realloc(config->buttons,
          config->button_count * sizeof(SbConfigButton));
        config->buttons[config->button_count - 1] = btn;
      } else {
        g_free(btn.name);
        g_free(btn.command);
        g_free(btn.icon);
      }
    } else if (strcmp(key, "keybind") == 0) {
      SbConfigKeybind kb = parse_keybind_value(value);
      if (kb.action < SB_ACTION_COUNT && kb.keyval != GDK_KEY_VoidSymbol) {
        config->keybind_count++;
        config->keybinds = g_realloc(config->keybinds,
          config->keybind_count * sizeof(SbConfigKeybind));
        config->keybinds[config->keybind_count - 1] = kb;
      }
    }
  }

  fclose(f);

  /* Ensure at least the default keybinds are always present */
  sb_config_add_defaults(config);

  g_printerr("[shellbar] loaded %d buttons, %d keybinds\n",
             config->button_count, config->keybind_count);
  return config;
}

void sb_config_free(SbConfig *config) {
  if (!config) return;
  for (int i = 0; i < config->button_count; i++) {
    g_free(config->buttons[i].name);
    g_free(config->buttons[i].command);
    g_free(config->buttons[i].icon);
  }
  g_free(config->buttons);
  g_free(config->keybinds);
  g_free(config);
}

const SbConfigKeybind *sb_config_find_keybind(SbConfig *config, guint keyval,
                                              GdkModifierType mods) {
  if (!config) return NULL;
  for (int i = 0; i < config->keybind_count; i++) {
    if (config->keybinds[i].keyval == keyval &&
        config->keybinds[i].mods == (mods & (GDK_CONTROL_MASK |
           GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)))
      return &config->keybinds[i];
  }
  return NULL;
}

static void write_quoted(FILE *f, const char *val) {
  fputc('"', f);
  for (const char *c = val; *c; c++) {
    if (*c == '"' || *c == '\\') fputc('\\', f);
    fputc(*c, f);
  }
  fputc('"', f);
}

void sb_config_save(SbConfig *config) {
  char *path = config_path();
  if (!path) return;

  /* Ensure directory exists */
  char *dir = g_path_get_dirname(path);
  g_mkdir_with_parents(dir, 0755);
  g_free(dir);

  FILE *f = fopen(path, "w");
  if (!f) { g_free(path); return; }

  fputs("# ShellBar Configuration\n", f);

  for (int i = 0; i < config->button_count; i++) {
    SbConfigButton *b = &config->buttons[i];
    if (!b->name || !b->command) continue;
    fprintf(f, "toolbar-button = name=");
    write_quoted(f, b->name);
    fputs(", command=", f);
    write_quoted(f, b->command);
    if (b->icon && b->icon[0]) {
      fputs(", icon=", f);
      write_quoted(f, b->icon);
    }
    fputc('\n', f);
  }

  for (int i = 0; i < config->keybind_count; i++) {
    SbConfigKeybind *kb = &config->keybinds[i];
    const char *act = "copy";
    switch (kb->action) {
      case SB_ACTION_COPY:       act = "copy"; break;
      case SB_ACTION_PASTE:      act = "paste"; break;
      case SB_ACTION_SELECT_ALL: act = "select_all"; break;
      default: continue;
    }
    const char *kname = gdk_keyval_name(kb->keyval);
    if (!kname) continue;
    char mods_buf[128] = "";
    if (kb->mods & GDK_CONTROL_MASK) strcat(mods_buf, "ctrl+");
    if (kb->mods & GDK_SHIFT_MASK)   strcat(mods_buf, "shift+");
    if (kb->mods & GDK_ALT_MASK)     strcat(mods_buf, "alt+");
    if (kb->mods & GDK_SUPER_MASK)   strcat(mods_buf, "super+");
    size_t mlen = strlen(mods_buf);
    if (mlen > 0) mods_buf[mlen - 1] = '\0';
    fprintf(f, "keybind = action=");
    write_quoted(f, act);
    fputs(", key=", f);
    write_quoted(f, kname);
    if (mods_buf[0]) {
      fputs(", mods=", f);
      write_quoted(f, mods_buf);
    }
    fputc('\n', f);
  }

  fclose(f);
  g_free(path);
}
