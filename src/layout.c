#include "layout.h"
#include "template.h"

static GHashTable *
date_tokens_new (GDateTime *date)
{
  GHashTable *tokens = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  g_hash_table_insert (tokens, "YYYY", g_strdup_printf ("%04d", g_date_time_get_year (date)));
  g_hash_table_insert (tokens, "MM", g_strdup_printf ("%02d", g_date_time_get_month (date)));
  g_hash_table_insert (tokens, "DD", g_strdup_printf ("%02d", g_date_time_get_day_of_month (date)));
  return tokens;
}

gchar *
layout_render_relative_path (const gchar *layout_template, GDateTime *date)
{
  GHashTable *tokens = date_tokens_new (date);
  gchar *result = template_render (layout_template ? layout_template : LAYOUT_DEFAULT_TEMPLATE, tokens);
  g_hash_table_unref (tokens);
  return result;
}

gchar *
layout_absolute_path_for_date (const Journal *journal, GDateTime *date)
{
  gchar *relative = layout_render_relative_path (journal->layout_template, date);
  gchar *absolute = g_build_filename (journal->root_dir, relative, NULL);
  g_free (relative);
  return absolute;
}

gchar *
layout_today_absolute_path (const Journal *journal)
{
  GDateTime *now = g_date_time_new_now_local ();
  gchar *path = layout_absolute_path_for_date (journal, now);
  g_date_time_unref (now);
  return path;
}

gchar *
layout_render_new_file_content (const Journal *journal, GDateTime *date)
{
  GHashTable *tokens = date_tokens_new (date);
  g_hash_table_insert (tokens, "DATE",
                        g_strdup_printf ("%04d-%02d-%02d",
                                          g_date_time_get_year (date),
                                          g_date_time_get_month (date),
                                          g_date_time_get_day_of_month (date)));
  g_hash_table_insert (tokens, "AUTHOR", g_strdup (journal->author ? journal->author : ""));

  gchar *result = template_render (journal->new_file_template ? journal->new_file_template : "", tokens);
  g_hash_table_unref (tokens);
  return result;
}
