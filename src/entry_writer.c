#include "entry_writer.h"
#include "layout.h"
#include "markdown_editor.h"

#include <glib/gstdio.h>
#include <errno.h>
#include <unistd.h>

gboolean
entry_writer_ensure_file_exists (const Journal *journal, GDateTime *date, GError **error)
{
  gchar *path = layout_absolute_path_for_date (journal, date);

  if (g_file_test (path, G_FILE_TEST_EXISTS))
    {
      g_free (path);
      return TRUE;
    }

  gchar *dir = g_path_get_dirname (path);
  if (g_mkdir_with_parents (dir, 0755) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "Could not create directory %s", dir);
      g_free (dir);
      g_free (path);
      return FALSE;
    }
  g_free (dir);

  gchar *content = layout_render_new_file_content (journal, date);
  gboolean ok = g_file_set_contents (path, content, -1, error);
  g_free (content);
  g_free (path);
  return ok;
}

gboolean
entry_writer_add_entries (const Journal *journal, GDateTime *date, const gchar *marker,
                           gint occurrence, EntryType entry_type, const gchar *entry_template,
                           gchar * const *entry_lines, GError **error)
{
  if (!entry_writer_ensure_file_exists (journal, date, error))
    return FALSE;

  gchar *path = layout_absolute_path_for_date (journal, date);

  gchar *original = NULL;
  if (!g_file_get_contents (path, &original, NULL, error))
    {
      g_free (path);
      return FALSE;
    }

  gchar *updated = markdown_editor_insert_entries (original, marker, occurrence, entry_type, entry_template, entry_lines);
  g_free (original);

  gchar *tmp_path = g_strdup_printf ("%s.tmp.%d", path, getpid ());

  GError *local_error = NULL;
  gboolean wrote = g_file_set_contents (tmp_path, updated, -1, &local_error);
  g_free (updated);

  if (!wrote)
    {
      g_propagate_error (error, local_error);
      g_unlink (tmp_path);
      g_free (tmp_path);
      g_free (path);
      return FALSE;
    }

  if (g_rename (tmp_path, path) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "Could not replace %s", path);
      g_unlink (tmp_path);
      g_free (tmp_path);
      g_free (path);
      return FALSE;
    }

  g_free (tmp_path);
  g_free (path);
  return TRUE;
}
