/*
 * ShellBar v1.0 — A command-bar terminal emulator built on libghostty-vt
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
  { "Storybook", "pnpm storybook\n", "media-playback-start" },
  { "Build",     "pnpm build\n",     "emblem-system" },
  { "Test",      "pnpm test\n",      "emblem-default" },
  { "Dev",       "pnpm dev\n",       "computer" },
  { "Lint",      "pnpm lint\n",      "emblem-important" },
};
static const int default_count = sizeof(default_buttons) / sizeof(default_buttons[0]);

void sb_config_add_defaults(SbConfig *config) {
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
    char *key = key_start;
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
    if (*p == ',') p++;
  }
  g_free(buf);
  return btn;
}

/* ------------------------------------------------------------------ */
/* Config loading                                                      */
/* ------------------------------------------------------------------ */

SbConfig *sb_config_load(void) {
  SbConfig *config = g_malloc0(sizeof(SbConfig));

  char *path = config_path();
  if (!path) return config;

  FILE *f = fopen(path, "r");
  if (!f) { g_free(path); return config; }
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
    }
  }

  fclose(f);
  g_printerr("[shellbar] loaded %d buttons\n", config->button_count);
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
  g_free(config);
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

  fclose(f);
  g_free(path);
}
