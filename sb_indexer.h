/*
 * ShellBar v1.8.0 — A command-bar terminal emulator built on libghostty-vt
 * Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
 * MIT License
 */
#ifndef SB_INDEXER_H
#define SB_INDEXER_H

#include <gtk/gtk.h>

typedef struct _SbIndexer SbIndexer;

SbIndexer *sb_indexer_new(void);
void sb_indexer_free(SbIndexer *self);
void sb_indexer_start_async(SbIndexer *self, GCancellable *cancellable,
                            GAsyncReadyCallback callback, gpointer user_data);
gboolean sb_indexer_start_finish(SbIndexer *self, GAsyncResult *result,
                                 GError **error);
GList *sb_indexer_get_matches(SbIndexer *self, const char *prefix);

#endif
