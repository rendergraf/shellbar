/*
 * ShellBar v1.9.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#include "sb_indexer.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Trie node                                                           */
/* ------------------------------------------------------------------ */

typedef struct _SbTrieNode {
  GHashTable *children;
  char *full_path;
} SbTrieNode;

static SbTrieNode *trie_node_new(void) {
  SbTrieNode *node = g_malloc0(sizeof(SbTrieNode));
  return node;
}

static void trie_node_free(SbTrieNode *node) {
  if (!node) return;
  if (node->children) {
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, node->children);
    while (g_hash_table_iter_next(&iter, NULL, &value))
      trie_node_free((SbTrieNode *)value);
    g_hash_table_unref(node->children);
  }
  g_free(node->full_path);
  g_free(node);
}

static void trie_insert(SbTrieNode *root, const char *name, const char *full_path) {
  SbTrieNode *cur = root;
  for (const char *p = name; *p; p++) {
    if (!cur->children) {
      cur->children = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                             NULL, NULL);
    }
    SbTrieNode *child = g_hash_table_lookup(cur->children,
                                             GINT_TO_POINTER((int)*p));
    if (!child) {
      child = trie_node_new();
      g_hash_table_insert(cur->children, GINT_TO_POINTER((int)*p), child);
    }
    cur = child;
  }
  if (!cur->full_path)
    cur->full_path = g_strdup(full_path);
}

static void trie_collect(SbTrieNode *node, GList **list) {
  if (!node) return;
  if (node->full_path)
    *list = g_list_prepend(*list, g_strdup(node->full_path));
  if (node->children) {
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, node->children);
    while (g_hash_table_iter_next(&iter, NULL, &value))
      trie_collect((SbTrieNode *)value, list);
  }
}

static SbTrieNode *trie_find(SbTrieNode *root, const char *prefix) {
  SbTrieNode *cur = root;
  for (const char *p = prefix; *p; p++) {
    if (!cur->children) return NULL;
    SbTrieNode *child = g_hash_table_lookup(cur->children,
                                             GINT_TO_POINTER((int)*p));
    if (!child) return NULL;
    cur = child;
  }
  return cur;
}

/* ------------------------------------------------------------------ */
/* Indexer struct                                                      */
/* ------------------------------------------------------------------ */

struct _SbIndexer {
  SbTrieNode *trie;
  GMutex mutex;
  gboolean ready;
};

/* ------------------------------------------------------------------ */
/* PATH scanning (runs in a GTask thread)                              */
/* ------------------------------------------------------------------ */

typedef struct {
  SbTrieNode *trie;
} ScanData;

static void scan_paths(GTask *task, gpointer source, gpointer task_data,
                        GCancellable *cancellable) {
  (void)source;
  ScanData *sd = task_data;

  const char *path_env = g_getenv("PATH");
  if (!path_env) {
    g_task_return_boolean(task, TRUE);
    return;
  }

  char *path_copy = g_strdup(path_env);
  char *saveptr = NULL;
  char *dir_path = strtok_r(path_copy, ":", &saveptr);

  while (dir_path) {
    if (g_cancellable_is_cancelled(cancellable)) {
      g_free(path_copy);
      g_task_return_boolean(task, FALSE);
      return;
    }

    DIR *dir = opendir(dir_path);
    if (dir) {
      struct dirent *entry;
      while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full, &st) == 0 && S_ISREG(st.st_mode) &&
            (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
          trie_insert(sd->trie, entry->d_name, full);
        }
      }
      closedir(dir);
    }
    dir_path = strtok_r(NULL, ":", &saveptr);
  }

  g_free(path_copy);
  g_task_return_boolean(task, TRUE);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

SbIndexer *sb_indexer_new(void) {
  SbIndexer *self = g_malloc0(sizeof(SbIndexer));
  self->trie = trie_node_new();
  g_mutex_init(&self->mutex);
  self->ready = FALSE;
  return self;
}

void sb_indexer_free(SbIndexer *self) {
  if (!self) return;
  trie_node_free(self->trie);
  g_mutex_clear(&self->mutex);
  g_free(self);
}

void sb_indexer_start_async(SbIndexer *self, GCancellable *cancellable,
                            GAsyncReadyCallback callback, gpointer user_data) {
  ScanData *sd = g_malloc0(sizeof(ScanData));
  sd->trie = self->trie;

  GTask *task = g_task_new(NULL, cancellable, callback, user_data);
  g_task_set_source_tag(task, sb_indexer_start_async);
  g_task_set_task_data(task, sd, g_free);
  g_task_run_in_thread(task, scan_paths);
  g_object_unref(task);
}

gboolean sb_indexer_start_finish(SbIndexer *self, GAsyncResult *result,
                                 GError **error) {
  (void)self;
  g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
  gboolean ok = g_task_propagate_boolean(G_TASK(result), error);
  if (ok) {
    g_mutex_lock(&self->mutex);
    self->ready = TRUE;
    g_mutex_unlock(&self->mutex);
  }
  return ok;
}

GList *sb_indexer_get_matches(SbIndexer *self, const char *prefix) {
  g_return_val_if_fail(self != NULL, NULL);

  g_mutex_lock(&self->mutex);
  if (!self->ready || !self->trie) {
    g_mutex_unlock(&self->mutex);
    return NULL;
  }

  GList *list = NULL;
  SbTrieNode *node = trie_find(self->trie, prefix);
  if (node)
    trie_collect(node, &list);
  g_mutex_unlock(&self->mutex);

  return list;
}
