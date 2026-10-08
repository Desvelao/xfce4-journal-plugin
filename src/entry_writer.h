#ifndef ENTRY_WRITER_H
#define ENTRY_WRITER_H

#include <glib.h>
#include "config.h"

/* Ensures the day file for `journal`/`date` exists: creates parent
 * directories (g_mkdir_with_parents, 0755) and writes
 * layout_render_new_file_content() verbatim if (and only if) the file does
 * not already exist. Never overwrites/truncates an existing file. Returns
 * TRUE with no error if the file already existed. */
gboolean entry_writer_ensure_file_exists (const Journal *journal, GDateTime *date, GError **error);

/* Full "add an entry" operation: ensures the day file exists (as above),
 * reads its current on-disk contents fresh, splices in `entry_lines` under
 * the `occurrence`-th match of `marker` via markdown_editor_insert_entries()
 * (1 = first match, the default), and writes the result back atomically:
 * <path>.tmp.<pid> in the same directory, then g_rename() into place — same
 * recipe as config_save_to_path but WITHOUT the chmod 0600: this is an
 * ordinary personal markdown file, often itself inside the user's own
 * git-tracked journal repo, so it keeps default umask permissions rather
 * than being locked to owner-only. */
gboolean entry_writer_add_entries (const Journal *journal, GDateTime *date,
                                    const gchar *marker, gint occurrence,
                                    EntryType entry_type, const gchar *entry_template,
                                    gchar * const *entry_lines, GError **error);

#endif
