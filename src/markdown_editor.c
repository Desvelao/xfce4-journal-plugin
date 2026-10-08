#include "markdown_editor.h"

#include <string.h>

MarkdownLineEnding
markdown_editor_detect_line_ending (const gchar *content)
{
  if (content && strstr (content, "\r\n"))
    return MARKDOWN_LINE_ENDING_CRLF;
  return MARKDOWN_LINE_ENDING_LF;
}

gchar **
markdown_editor_split_lines (const gchar *content)
{
  gchar **lines = g_strsplit (content ? content : "", "\n", -1);
  for (gchar **p = lines; p && *p; p++)
    {
      gsize len = strlen (*p);
      if (len > 0 && (*p)[len - 1] == '\r')
        (*p)[len - 1] = '\0';
    }
  return lines;
}

gchar *
markdown_editor_join_lines (gchar * const *lines, MarkdownLineEnding ending)
{
  const gchar *sep = (ending == MARKDOWN_LINE_ENDING_CRLF) ? "\r\n" : "\n";

  guint n = 0;
  for (gchar * const *p = lines; p && *p; p++)
    n++;

  GString *out = g_string_new (NULL);
  for (guint i = 0; i < n; i++)
    {
      g_string_append (out, lines[i]);
      if (i + 1 < n)
        g_string_append (out, sep);
    }
  return g_string_free (out, FALSE);
}

static gboolean
looks_like_atx_header (const gchar *line)
{
  gint hashes = 0;
  while (line[hashes] == '#')
    hashes++;
  if (hashes < 1 || hashes > 6)
    return FALSE;
  gchar next = line[hashes];
  return next == ' ' || next == '\t';
}

/* A "bold-only" line: the whole trimmed line is wrapped in a matching pair
 * of single or double asterisks with no other asterisk inside, e.g.
 * "*Reviews*" or "**Reviews**". */
static gboolean
looks_like_bold_header (const gchar *line)
{
  gsize len = strlen (line);
  gsize end = len;
  while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t'))
    end--;
  if (end < 3)
    return FALSE;

  gint lead = 0;
  while (lead < 2 && (gsize) lead < end && line[lead] == '*')
    lead++;
  if (lead == 0)
    return FALSE;

  gint trail = 0;
  while (trail < 2 && (gsize) (lead + trail) < end && line[end - 1 - trail] == '*')
    trail++;
  if (trail != lead)
    return FALSE;

  gsize mid_start = (gsize) lead;
  gsize mid_end = end - (gsize) trail;
  if (mid_end <= mid_start)
    return FALSE;

  for (gsize i = mid_start; i < mid_end; i++)
    if (line[i] == '*')
      return FALSE;

  return TRUE;
}

gboolean
markdown_editor_line_looks_like_header (const gchar *line)
{
  if (!line)
    return FALSE;
  return looks_like_atx_header (line) || looks_like_bold_header (line);
}

gboolean
markdown_editor_line_matches_marker (const gchar *line, const gchar *marker)
{
  if (!line || !marker)
    return FALSE;

  gchar *line_copy = g_strdup (line);
  gsize len = strlen (line_copy);
  if (len > 0 && line_copy[len - 1] == '\r')
    line_copy[len - 1] = '\0';
  g_strstrip (line_copy);

  gchar *marker_copy = g_strdup (marker);
  g_strstrip (marker_copy);

  gboolean match = g_strcmp0 (line_copy, marker_copy) == 0;

  g_free (line_copy);
  g_free (marker_copy);
  return match;
}

MarkdownInsertionPoint
markdown_editor_find_insertion_point (gchar * const *lines, const gchar *marker, gint occurrence)
{
  MarkdownInsertionPoint result = { FALSE, 0 };

  if (occurrence < 1)
    occurrence = 1;

  guint n = 0;
  for (gchar * const *p = lines; p && *p; p++)
    n++;

  gint marker_index = -1;
  gint remaining = occurrence;
  for (guint i = 0; i < n; i++)
    {
      if (markdown_editor_line_matches_marker (lines[i], marker))
        {
          remaining--;
          if (remaining == 0)
            {
              marker_index = (gint) i;
              break;
            }
        }
    }

  if (marker_index < 0)
    {
      result.marker_found = FALSE;
      result.insert_index = (gint) n;
      return result;
    }

  result.marker_found = TRUE;
  gint insert = (gint) n;
  for (gint i = marker_index + 1; i < (gint) n; i++)
    {
      if (markdown_editor_line_looks_like_header (lines[i]))
        {
          insert = i;
          break;
        }
    }
  result.insert_index = insert;
  return result;
}

gchar *
markdown_editor_format_entry (EntryType entry_type, const gchar *entry_template, const gchar *text)
{
  if (entry_type == ENTRY_TYPE_TODO)
    return g_strdup_printf ("- [ ] %s", text);

  if (entry_type == ENTRY_TYPE_CUSTOM && entry_template && strstr (entry_template, ENTRY_PLACEHOLDER))
    {
      gchar **parts = g_strsplit (entry_template, ENTRY_PLACEHOLDER, -1);
      gchar *result = g_strjoinv (text, parts);
      g_strfreev (parts);
      return result;
    }

  return g_strdup_printf ("- %s", text);
}

gchar *
markdown_editor_insert_entries (const gchar *original_content,
                                 const gchar *marker,
                                 gint occurrence,
                                 EntryType entry_type,
                                 const gchar *entry_template,
                                 gchar * const *entry_lines)
{
  const gchar *content = original_content ? original_content : "";
  MarkdownLineEnding ending = markdown_editor_detect_line_ending (content);
  gchar **lines = markdown_editor_split_lines (content);

  GPtrArray *bullets = g_ptr_array_new_with_free_func (g_free);
  for (gchar * const *p = entry_lines; p && *p; p++)
    {
      gchar *trimmed = g_strdup (*p);
      g_strstrip (trimmed);
      if (*trimmed)
        g_ptr_array_add (bullets, markdown_editor_format_entry (entry_type, entry_template, trimmed));
      g_free (trimmed);
    }

  MarkdownInsertionPoint point = markdown_editor_find_insertion_point (lines, marker, occurrence);

  guint n = 0;
  for (gchar * const *p = lines; p && *p; p++)
    n++;

  GPtrArray *result_lines = g_ptr_array_new_with_free_func (g_free);

  if (point.marker_found)
    {
      for (guint i = 0; i < n; i++)
        {
          if ((gint) i == point.insert_index)
            for (guint b = 0; b < bullets->len; b++)
              g_ptr_array_add (result_lines, g_strdup (g_ptr_array_index (bullets, b)));
          g_ptr_array_add (result_lines, g_strdup (lines[i]));
        }
      if (point.insert_index == (gint) n)
        for (guint b = 0; b < bullets->len; b++)
          g_ptr_array_add (result_lines, g_strdup (g_ptr_array_index (bullets, b)));
    }
  else
    {
      for (guint i = 0; i < n; i++)
        g_ptr_array_add (result_lines, g_strdup (lines[i]));

      gboolean needs_blank_separator = (n > 0) && (*lines[n - 1] != '\0');
      if (needs_blank_separator)
        g_ptr_array_add (result_lines, g_strdup (""));

      g_ptr_array_add (result_lines, g_strdup (marker));
      for (guint b = 0; b < bullets->len; b++)
        g_ptr_array_add (result_lines, g_strdup (g_ptr_array_index (bullets, b)));
    }

  g_ptr_array_add (result_lines, NULL);
  gchar *joined = markdown_editor_join_lines ((gchar * const *) result_lines->pdata, ending);

  g_ptr_array_unref (bullets);
  g_ptr_array_unref (result_lines);
  g_strfreev (lines);

  return joined;
}
