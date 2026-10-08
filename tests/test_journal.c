/* Pure-function unit tests: no GTK, no panel, no filesystem access to the
 * real ~/.config (config/entry_writer tests use a temp directory via
 * config_load_from_path/config_save_to_path and a scratch Journal root). */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "../src/config.h"
#include "../src/template.h"
#include "../src/layout.h"
#include "../src/markdown_editor.h"
#include "../src/entry_writer.h"

/* A representative day file (names and links anonymized), which is the
 * shape this whole plugin was designed against. */
static const gchar *SAMPLE_FILE_LF =
  "# Report\n"
  "*Jane Doe 2026-09-02*\n"
  "*Development*\n"
  "*Triage*\n"
  "*Newly opened*\n"
  "*Worked on*\n"
  "\n"
  "*Reviews*\n"
  "- https://github.com/example/repo-a/pull/1\n"
  "- https://github.com/example/repo-b/pull/2\n"
  "\n"
  "\n"
  "*Operational*\n"
  "*Triage*\n"
  "*Newly opened*\n"
  "*Worked on*\n"
  "- https://github.com/example/repo-a/issues/3\n"
  "*Reviews*\n"
  "\n"
  "*Meetings*\n"
  "\n"
  "\n"
  "\n"
  "*Miscellaneous*\n"
  "\n"
  "\n"
  "# My things\n"
  "\n"
  "## TODO\n"
  "\n"
  "### On hold\n"
  "\n"
  "## Issue\n"
  "\n"
  "## Community\n"
  "\n"
  "## Meeting\n"
  "\n"
  "## Daily\n"
  "\n"
  "## Miscellaneous\n"
  "\n"
  "some-command --flag value\n"
  "\n"
  "- first example note\n"
  "- second example note";

static gchar *
crlf_variant (const gchar *lf_content)
{
  gchar **parts = g_strsplit (lf_content, "\n", -1);
  gchar *joined = g_strjoinv ("\r\n", parts);
  g_strfreev (parts);
  return joined;
}

/* --- template --- */

static void
test_template_known_and_unknown_tokens (void)
{
  GHashTable *tokens = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (tokens, "NAME", "World");

  gchar *result = template_render ("Hello, {NAME}! {UNKNOWN} stays.", tokens);
  g_assert_cmpstr (result, ==, "Hello, World! {UNKNOWN} stays.");

  g_free (result);
  g_hash_table_unref (tokens);
}

static void
test_template_adjacent_tokens (void)
{
  GHashTable *tokens = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (tokens, "YYYY", "2026");
  g_hash_table_insert (tokens, "MM", "09");
  g_hash_table_insert (tokens, "DD", "02");

  gchar *result = template_render ("{YYYY}-{MM}-{DD}.md", tokens);
  g_assert_cmpstr (result, ==, "2026-09-02.md");

  g_free (result);
  g_hash_table_unref (tokens);
}

static void
test_template_unmatched_brace_preserved (void)
{
  GHashTable *tokens = g_hash_table_new (g_str_hash, g_str_equal);
  gchar *result = template_render ("code: { not closed", tokens);
  g_assert_cmpstr (result, ==, "code: { not closed");
  g_free (result);
  g_hash_table_unref (tokens);
}

/* --- layout --- */

static void
test_layout_default_template (void)
{
  GTimeZone *tz = g_time_zone_new_utc ();
  GDateTime *date = g_date_time_new (tz, 2026, 9, 2, 0, 0, 0.0);
  g_time_zone_unref (tz);

  gchar *path = layout_render_relative_path (LAYOUT_DEFAULT_TEMPLATE, date);
  g_assert_cmpstr (path, ==, "2026/09/2026-09-02.md");

  g_free (path);
  g_date_time_unref (date);
}

static void
test_layout_flat_template_and_zero_padding (void)
{
  GTimeZone *tz = g_time_zone_new_utc ();
  GDateTime *date = g_date_time_new (tz, 2026, 1, 5, 0, 0, 0.0);
  g_time_zone_unref (tz);

  gchar *path = layout_render_relative_path ("{YYYY}-{MM}-{DD}.md", date);
  g_assert_cmpstr (path, ==, "2026-01-05.md");

  g_free (path);
  g_date_time_unref (date);
}

static void
test_layout_new_file_content_tokens (void)
{
  Journal *journal = journal_new ();
  journal->author = g_strdup ("Ada Lovelace");
  journal->new_file_template = g_strdup ("# {DATE}\nby {AUTHOR} ({YYYY}/{MM}/{DD})\n");

  GTimeZone *tz = g_time_zone_new_utc ();
  GDateTime *date = g_date_time_new (tz, 2026, 9, 2, 0, 0, 0.0);
  g_time_zone_unref (tz);

  gchar *content = layout_render_new_file_content (journal, date);
  g_assert_cmpstr (content, ==, "# 2026-09-02\nby Ada Lovelace (2026/09/02)\n");

  g_free (content);
  g_date_time_unref (date);
  journal_free (journal);
}

static void
test_layout_new_file_content_empty_template (void)
{
  Journal *journal = journal_new ();
  GTimeZone *tz = g_time_zone_new_utc ();
  GDateTime *date = g_date_time_new (tz, 2026, 9, 2, 0, 0, 0.0);
  g_time_zone_unref (tz);

  gchar *content = layout_render_new_file_content (journal, date);
  g_assert_cmpstr (content, ==, "");

  g_free (content);
  g_date_time_unref (date);
  journal_free (journal);
}

/* --- markdown_editor: line ending detection --- */

static void
test_detect_line_ending_crlf (void)
{
  g_assert_cmpint (markdown_editor_detect_line_ending ("a\r\nb\r\n"), ==, MARKDOWN_LINE_ENDING_CRLF);
}

static void
test_detect_line_ending_lf (void)
{
  g_assert_cmpint (markdown_editor_detect_line_ending ("a\nb\n"), ==, MARKDOWN_LINE_ENDING_LF);
}

static void
test_detect_line_ending_empty (void)
{
  g_assert_cmpint (markdown_editor_detect_line_ending (""), ==, MARKDOWN_LINE_ENDING_LF);
}

/* --- markdown_editor: header detection --- */

static void
test_looks_like_header_atx (void)
{
  g_assert_true (markdown_editor_line_looks_like_header ("## TODO"));
  g_assert_true (markdown_editor_line_looks_like_header ("### On hold"));
  g_assert_true (markdown_editor_line_looks_like_header ("# Report"));
}

static void
test_looks_like_header_bold (void)
{
  g_assert_true (markdown_editor_line_looks_like_header ("*Reviews*"));
  g_assert_true (markdown_editor_line_looks_like_header ("**Reviews**"));
}

static void
test_looks_like_header_rejects_non_headers (void)
{
  g_assert_false (markdown_editor_line_looks_like_header ("- bullet"));
  g_assert_false (markdown_editor_line_looks_like_header ("Report")); /* plain, no punctuation */
  g_assert_false (markdown_editor_line_looks_like_header (""));
  g_assert_false (markdown_editor_line_looks_like_header ("***"));
}

/* --- markdown_editor: marker matching --- */

static void
test_marker_matches_exact (void)
{
  g_assert_true (markdown_editor_line_matches_marker ("## TODO", "## TODO"));
}

static void
test_marker_matches_trailing_cr (void)
{
  g_assert_true (markdown_editor_line_matches_marker ("## TODO\r", "## TODO"));
}

static void
test_marker_matches_whitespace_trim (void)
{
  g_assert_true (markdown_editor_line_matches_marker ("  ## TODO  ", "## TODO"));
}

static void
test_marker_matches_case_sensitive (void)
{
  g_assert_false (markdown_editor_line_matches_marker ("## todo", "## TODO"));
}

/* --- markdown_editor: worked examples against the real sample file --- */

static void
test_insert_under_todo_lands_between_headers (void)
{
  /* The sample has one blank line between "## TODO" and "### On hold".
   * Insertion happens immediately before the next header-like line, i.e.
   * after that existing blank line, not before it. */
  gchar *entries[] = { "wztool_sync_branches follow-up", NULL };
  gchar *result = markdown_editor_insert_entries (SAMPLE_FILE_LF, "## TODO", 1, ENTRY_TYPE_BULLET, NULL, entries);

  g_assert_nonnull (strstr (result, "## TODO\n\n- wztool_sync_branches follow-up\n### On hold\n"));
  g_free (result);
}

static void
test_insert_under_reviews_occurrence_1_targets_development (void)
{
  gchar *entries[] = { "https://github.com/example/repo-a/pull/9999", NULL };
  gchar *result = markdown_editor_insert_entries (SAMPLE_FILE_LF, "*Reviews*", 1, ENTRY_TYPE_BULLET, NULL, entries);

  /* Insertion happens immediately before the next header-like line
   * (*Operational*), i.e. after the two existing blank lines that already
   * separate the bullets from it in the sample — not before them. */
  g_assert_nonnull (strstr (result,
      "- https://github.com/example/repo-b/pull/2\n"
      "\n"
      "\n"
      "- https://github.com/example/repo-a/pull/9999\n"
      "*Operational*\n"));

  /* The second *Reviews* occurrence (near *Meetings*) must be untouched:
   * exactly one copy of the new URL appears anywhere in the result. */
  gchar *first = strstr (result, "pull/9999");
  g_assert_nonnull (first);
  g_assert_null (strstr (first + 1, "pull/9999"));

  g_free (result);
}

static void
test_insert_under_reviews_occurrence_2_targets_operational (void)
{
  /* The second "*Reviews*" (under *Operational*, just before *Meetings*)
   * has an empty body in the sample — one blank line, then the next
   * header. occurrence=2 must land there, not under *Development*. */
  gchar *entries[] = { "https://github.com/example/repo-a/pull/8888", NULL };
  gchar *result = markdown_editor_insert_entries (SAMPLE_FILE_LF, "*Reviews*", 2, ENTRY_TYPE_BULLET, NULL, entries);

  g_assert_nonnull (strstr (result,
      "*Reviews*\n"
      "\n"
      "- https://github.com/example/repo-a/pull/8888\n"
      "*Meetings*\n"));

  /* The first *Reviews* section (Development, with its two pre-existing
   * bullets) must be untouched. */
  g_assert_null (strstr (result, "pull/8888\n*Operational*"));
  gchar *first = strstr (result, "pull/8888");
  g_assert_nonnull (first);
  g_assert_null (strstr (first + 1, "pull/8888"));

  g_free (result);
}

static void
test_insert_occurrence_beyond_available_matches_falls_back (void)
{
  /* "*Reviews*" appears exactly twice in the sample; requesting the third
   * occurrence must behave exactly like a marker that isn't present at
   * all — append at EOF, nothing earlier touched. */
  gchar *entries[] = { "x", NULL };
  gchar *result = markdown_editor_insert_entries (SAMPLE_FILE_LF, "*Reviews*", 3, ENTRY_TYPE_BULLET, NULL, entries);

  gchar *expected = g_strconcat (SAMPLE_FILE_LF, "\n\n*Reviews*\n- x", NULL);
  g_assert_cmpstr (result, ==, expected);

  g_free (expected);
  g_free (result);
}

static void
test_insert_occurrence_zero_behaves_like_one (void)
{
  gchar *entries[] = { "wztool_sync_branches follow-up", NULL };
  gchar *result_zero = markdown_editor_insert_entries (SAMPLE_FILE_LF, "## TODO", 0, ENTRY_TYPE_BULLET, NULL, entries);
  gchar *result_one = markdown_editor_insert_entries (SAMPLE_FILE_LF, "## TODO", 1, ENTRY_TYPE_BULLET, NULL, entries);

  g_assert_cmpstr (result_zero, ==, result_one);

  g_free (result_zero);
  g_free (result_one);
}

static void
test_insert_marker_not_found_falls_back_to_append (void)
{
  gchar *entries[] = { "x", NULL };
  gchar *result = markdown_editor_insert_entries (SAMPLE_FILE_LF, "## Nonexistent", 1, ENTRY_TYPE_BULLET, NULL, entries);

  gchar *expected = g_strconcat (SAMPLE_FILE_LF, "\n\n## Nonexistent\n- x", NULL);
  g_assert_cmpstr (result, ==, expected);

  g_free (expected);
  g_free (result);
}

static void
test_insert_preserves_crlf (void)
{
  gchar *sample_crlf = crlf_variant (SAMPLE_FILE_LF);
  gchar *entries[] = { "follow-up", NULL };
  gchar *result = markdown_editor_insert_entries (sample_crlf, "## TODO", 1, ENTRY_TYPE_BULLET, NULL, entries);

  g_assert_nonnull (strstr (result, "## TODO\r\n\r\n- follow-up\r\n### On hold\r\n"));
  g_assert_cmpint (markdown_editor_detect_line_ending (result), ==, MARKDOWN_LINE_ENDING_CRLF);

  g_free (sample_crlf);
  g_free (result);
}

static void
test_insert_multiline_filters_blanks_and_preserves_order (void)
{
  gchar *entries[] = { "first", "", "  ", "second", NULL };
  gchar *result = markdown_editor_insert_entries (SAMPLE_FILE_LF, "## TODO", 1, ENTRY_TYPE_BULLET, NULL, entries);

  g_assert_nonnull (strstr (result, "## TODO\n\n- first\n- second\n### On hold\n"));
  g_free (result);
}

/* --- entry_writer (touches disk, via a scratch temp directory) --- */

static gchar *
make_temp_journal_root (void)
{
  return g_dir_make_tmp ("xfce4-journal-plugin-test-XXXXXX", NULL);
}

static void
remove_dir_recursive (const gchar *path)
{
  GDir *dir = g_dir_open (path, 0, NULL);
  if (!dir)
    {
      g_remove (path);
      return;
    }
  const gchar *name;
  while ((name = g_dir_read_name (dir)))
    {
      gchar *child = g_build_filename (path, name, NULL);
      if (g_file_test (child, G_FILE_TEST_IS_DIR))
        remove_dir_recursive (child);
      else
        g_remove (child);
      g_free (child);
    }
  g_dir_close (dir);
  g_rmdir (path);
}

static void
test_entry_writer_ensure_file_exists_creates_file_and_dirs (void)
{
  gchar *root = make_temp_journal_root ();
  Journal *journal = journal_new ();
  journal->root_dir = g_strdup (root);
  journal->layout_template = g_strdup (LAYOUT_DEFAULT_TEMPLATE);
  journal->new_file_template = g_strdup ("# {DATE}\n");

  GTimeZone *tz = g_time_zone_new_utc ();
  GDateTime *date = g_date_time_new (tz, 2026, 9, 2, 0, 0, 0.0);
  g_time_zone_unref (tz);

  GError *error = NULL;
  g_assert_true (entry_writer_ensure_file_exists (journal, date, &error));
  g_assert_no_error (error);

  gchar *path = layout_absolute_path_for_date (journal, date);
  g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));

  gchar *contents = NULL;
  g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
  g_assert_cmpstr (contents, ==, "# 2026-09-02\n");

  /* Calling again is a no-op: content is untouched. */
  g_assert_true (entry_writer_ensure_file_exists (journal, date, &error));
  gchar *contents2 = NULL;
  g_assert_true (g_file_get_contents (path, &contents2, NULL, NULL));
  g_assert_cmpstr (contents2, ==, contents);

  g_free (contents);
  g_free (contents2);
  g_free (path);
  g_date_time_unref (date);
  journal_free (journal);
  remove_dir_recursive (root);
  g_free (root);
}

static void
test_entry_writer_add_entries_creates_then_inserts (void)
{
  gchar *root = make_temp_journal_root ();
  Journal *journal = journal_new ();
  journal->root_dir = g_strdup (root);
  journal->layout_template = g_strdup (LAYOUT_DEFAULT_TEMPLATE);
  journal->new_file_template = g_strdup ("## TODO\n");

  GTimeZone *tz = g_time_zone_new_utc ();
  GDateTime *date = g_date_time_new (tz, 2026, 9, 2, 0, 0, 0.0);
  g_time_zone_unref (tz);

  gchar *entries[] = { "buy milk", NULL };
  GError *error = NULL;
  g_assert_true (entry_writer_add_entries (journal, date, "## TODO", 1, ENTRY_TYPE_BULLET, NULL, entries, &error));
  g_assert_no_error (error);

  gchar *path = layout_absolute_path_for_date (journal, date);
  /* The template "## TODO\n" splits into two lines ("## TODO" and a
   * trailing empty line at EOF); with no header-like line following the
   * marker, the new bullet lands at true EOF, after that trailing blank. */
  gchar *contents = NULL;
  g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
  g_assert_cmpstr (contents, ==, "## TODO\n\n- buy milk");

  g_free (contents);
  g_free (path);
  g_date_time_unref (date);
  journal_free (journal);
  remove_dir_recursive (root);
  g_free (root);
}

/* --- config --- */

static gchar *
make_temp_config_path (void)
{
  gchar *dir = g_dir_make_tmp ("xfce4-journal-plugin-config-test-XXXXXX", NULL);
  gchar *path = g_build_filename (dir, "config.json", NULL);
  g_free (dir);
  return path;
}

static void
test_config_round_trip (void)
{
  gchar *path = make_temp_config_path ();

  Config *cfg = g_new0 (Config, 1);
  cfg->config_schema_version = 1;

  Journal *j1 = journal_new ();
  j1->id = g_strdup ("journal-1");
  j1->name = g_strdup ("Wörk"); /* non-ASCII */
  j1->root_dir = g_strdup ("/tmp/journal1");
  j1->layout_template = g_strdup (LAYOUT_DEFAULT_TEMPLATE);
  j1->author = g_strdup ("Jane");
  j1->new_file_template = g_strdup ("# {DATE}\n\n## TODO\n");

  const gchar *labels[] = { "TODO", "On hold", "Issue", "Community", "Meeting", "Daily", "Miscellaneous" };
  const gchar *markers[] = { "## TODO", "### On hold", "## Issue", "## Community", "## Meeting", "## Daily", "## Miscellaneous" };
  for (guint i = 0; i < G_N_ELEMENTS (labels); i++)
    {
      Category *category = category_new ();
      category->id = g_strdup_printf ("cat-%u", i);
      category->label = g_strdup (labels[i]);
      category->marker = g_strdup (markers[i]);
      j1->categories = g_list_append (j1->categories, category);
    }

  Category *reviews_ops = category_new ();
  reviews_ops->id = g_strdup ("cat-reviews-ops");
  reviews_ops->label = g_strdup ("Reviews (Operational)");
  reviews_ops->marker = g_strdup ("*Reviews*");
  reviews_ops->marker_occurrence = 2;
  j1->categories = g_list_append (j1->categories, reviews_ops);

  Journal *j2 = journal_new ();
  j2->id = g_strdup ("journal-2");
  j2->name = g_strdup ("Empty Journal");
  j2->root_dir = g_strdup ("/tmp/journal2");
  j2->layout_template = g_strdup ("{YYYY}-{MM}-{DD}.md");

  cfg->journals = g_list_append (g_list_append (NULL, j1), j2);

  GError *error = NULL;
  gboolean saved = config_save_to_path (cfg, path, &error);
  g_assert_true (saved);
  g_assert_no_error (error);

  Config *loaded = config_load_from_path (path);
  g_assert_cmpint (loaded->config_schema_version, ==, 1);
  g_assert_cmpuint (g_list_length (loaded->journals), ==, 2);

  Journal *loaded_j1 = config_find_journal (loaded, "journal-1");
  g_assert_nonnull (loaded_j1);
  g_assert_cmpstr (loaded_j1->name, ==, "Wörk");
  g_assert_cmpstr (loaded_j1->author, ==, "Jane");
  g_assert_cmpuint (g_list_length (loaded_j1->categories), ==, 8);
  Category *loaded_cat0 = loaded_j1->categories->data;
  g_assert_cmpstr (loaded_cat0->label, ==, "TODO");
  g_assert_cmpstr (loaded_cat0->marker, ==, "## TODO");
  g_assert_cmpint (loaded_cat0->marker_occurrence, ==, 1);

  Category *loaded_reviews_ops = journal_find_category (loaded_j1, "cat-reviews-ops");
  g_assert_nonnull (loaded_reviews_ops);
  g_assert_cmpstr (loaded_reviews_ops->marker, ==, "*Reviews*");
  g_assert_cmpint (loaded_reviews_ops->marker_occurrence, ==, 2);

  Journal *loaded_j2 = config_find_journal (loaded, "journal-2");
  g_assert_nonnull (loaded_j2);
  g_assert_cmpstr (loaded_j2->author, ==, ""); /* missing key defaults to empty, not NULL */
  g_assert_null (loaded_j2->categories);

  config_free (cfg);
  config_free (loaded);
  g_unlink (path);
  gchar *dir = g_path_get_dirname (path);
  g_rmdir (dir);
  g_free (dir);
  g_free (path);
}

static void
test_config_missing_optional_fields_load_cleanly (void)
{
  gchar *path = make_temp_config_path ();
  const gchar *raw =
    "{\"config_schema_version\":1,\"journals\":["
    "{\"id\":\"j\",\"name\":\"Old Journal\",\"root_dir\":\"/tmp/x\",\"categories\":["
    "{\"id\":\"c\",\"marker\":\"## TODO\"}"
    "]}"
    "]}";
  g_file_set_contents (path, raw, -1, NULL);

  Config *cfg = config_load_from_path (path);
  Journal *j = config_find_journal (cfg, "j");
  g_assert_nonnull (j);
  g_assert_cmpstr (j->layout_template, ==, LAYOUT_DEFAULT_TEMPLATE);
  g_assert_cmpstr (j->author, ==, "");
  g_assert_cmpuint (g_list_length (j->categories), ==, 1);

  /* A category written before marker_occurrence existed has no such key at
   * all — must default to 1 (the first match), not 0. */
  Category *c = journal_find_category (j, "c");
  g_assert_nonnull (c);
  g_assert_cmpint (c->marker_occurrence, ==, 1);
  g_assert_cmpint (c->entry_type, ==, ENTRY_TYPE_BULLET);
  g_assert_null (c->entry_template);

  config_free (cfg);
  g_unlink (path);
  gchar *dir = g_path_get_dirname (path);
  g_rmdir (dir);
  g_free (dir);
  g_free (path);
}

static void
test_config_malformed_file_backs_up_and_returns_defaults (void)
{
  gchar *path = make_temp_config_path ();
  g_file_set_contents (path, "{ not valid json ", -1, NULL);

  g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*malformed config*");
  Config *cfg = config_load_from_path (path);
  g_test_assert_expected_messages ();

  g_assert_cmpint (cfg->config_schema_version, ==, 1);
  g_assert_null (cfg->journals);

  gchar *backup_path = g_strdup_printf ("%s.bak", path);
  g_assert_true (g_file_test (backup_path, G_FILE_TEST_IS_REGULAR));

  config_free (cfg);
  g_unlink (backup_path);
  g_free (backup_path);
  gchar *dir = g_path_get_dirname (path);
  g_rmdir (dir);
  g_free (dir);
  g_free (path);
}

static void
test_format_entry_types (void)
{
  gchar *b = markdown_editor_format_entry (ENTRY_TYPE_BULLET, NULL, "x");
  gchar *t = markdown_editor_format_entry (ENTRY_TYPE_TODO, NULL, "x");
  gchar *c = markdown_editor_format_entry (ENTRY_TYPE_CUSTOM, "* [ ] $ENTRY (again: $ENTRY) #w", "x");
  gchar *bad = markdown_editor_format_entry (ENTRY_TYPE_CUSTOM, "no placeholder", "x");
  g_assert_cmpstr (b, ==, "- x");
  g_assert_cmpstr (t, ==, "- [ ] x");
  g_assert_cmpstr (c, ==, "* [ ] x (again: x) #w");
  g_assert_cmpstr (bad, ==, "- x");
  g_free (b); g_free (t); g_free (c); g_free (bad);
}

static void
test_insert_todo_type (void)
{
  gchar *entries[] = { "a", "  ", "b", NULL };
  gchar *result = markdown_editor_insert_entries ("## TODO\n\n## Next\n", "## TODO", 1, ENTRY_TYPE_TODO, NULL, entries);
  g_assert_cmpstr (result, ==, "## TODO\n\n- [ ] a\n- [ ] b\n## Next\n");
  g_free (result);
}

static void
test_config_entry_type_round_trip (void)
{
  gchar *path = make_temp_config_path ();
  Config *cfg = g_new0 (Config, 1);
  cfg->config_schema_version = 1;
  Journal *j = journal_new ();
  j->id = g_strdup ("j"); j->name = g_strdup ("J"); j->root_dir = g_strdup ("/tmp/x");
  j->layout_template = g_strdup (LAYOUT_DEFAULT_TEMPLATE);
  const gchar *ids[] = { "t", "c" };
  for (int i = 0; i < 2; i++)
    {
      Category *c = category_new ();
      c->id = g_strdup (ids[i]); c->marker = g_strdup ("## M");
      c->entry_type = i ? ENTRY_TYPE_CUSTOM : ENTRY_TYPE_TODO;
      if (i) c->entry_template = g_strdup ("> $ENTRY");
      j->categories = g_list_append (j->categories, c);
    }
  cfg->journals = g_list_append (NULL, j);
  g_assert_true (config_save_to_path (cfg, path, NULL));
  config_free (cfg);

  cfg = config_load_from_path (path);
  j = config_find_journal (cfg, "j");
  g_assert_cmpint (journal_find_category (j, "t")->entry_type, ==, ENTRY_TYPE_TODO);
  Category *c = journal_find_category (j, "c");
  g_assert_cmpint (c->entry_type, ==, ENTRY_TYPE_CUSTOM);
  g_assert_cmpstr (c->entry_template, ==, "> $ENTRY");
  config_free (cfg);
  g_free (path);
}

static void
test_config_custom_without_placeholder_falls_back (void)
{
  gchar *path = make_temp_config_path ();
  g_file_set_contents (path,
    "{\"config_schema_version\":1,\"journals\":[{\"id\":\"j\",\"name\":\"J\",\"root_dir\":\"/tmp/x\","
    "\"categories\":[{\"id\":\"c\",\"marker\":\"## M\",\"entry_type\":\"custom\",\"entry_template\":\"nope\"}]}]}",
    -1, NULL);
  Config *cfg = config_load_from_path (path);
  Category *c = journal_find_category (config_find_journal (cfg, "j"), "c");
  g_assert_cmpint (c->entry_type, ==, ENTRY_TYPE_BULLET);
  g_assert_null (c->entry_template);
  config_free (cfg);
  g_free (path);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/entry/format_types", test_format_entry_types);
  g_test_add_func ("/entry/insert_todo", test_insert_todo_type);
  g_test_add_func ("/config/entry_type_round_trip", test_config_entry_type_round_trip);
  g_test_add_func ("/config/custom_without_placeholder", test_config_custom_without_placeholder_falls_back);
  g_test_add_func ("/template/known_and_unknown", test_template_known_and_unknown_tokens);
  g_test_add_func ("/template/adjacent_tokens", test_template_adjacent_tokens);
  g_test_add_func ("/template/unmatched_brace", test_template_unmatched_brace_preserved);

  g_test_add_func ("/layout/default_template", test_layout_default_template);
  g_test_add_func ("/layout/flat_template_zero_padding", test_layout_flat_template_and_zero_padding);
  g_test_add_func ("/layout/new_file_content_tokens", test_layout_new_file_content_tokens);
  g_test_add_func ("/layout/new_file_content_empty_template", test_layout_new_file_content_empty_template);

  g_test_add_func ("/markdown_editor/detect_line_ending_crlf", test_detect_line_ending_crlf);
  g_test_add_func ("/markdown_editor/detect_line_ending_lf", test_detect_line_ending_lf);
  g_test_add_func ("/markdown_editor/detect_line_ending_empty", test_detect_line_ending_empty);

  g_test_add_func ("/markdown_editor/looks_like_header_atx", test_looks_like_header_atx);
  g_test_add_func ("/markdown_editor/looks_like_header_bold", test_looks_like_header_bold);
  g_test_add_func ("/markdown_editor/looks_like_header_rejects", test_looks_like_header_rejects_non_headers);

  g_test_add_func ("/markdown_editor/marker_exact", test_marker_matches_exact);
  g_test_add_func ("/markdown_editor/marker_trailing_cr", test_marker_matches_trailing_cr);
  g_test_add_func ("/markdown_editor/marker_whitespace_trim", test_marker_matches_whitespace_trim);
  g_test_add_func ("/markdown_editor/marker_case_sensitive", test_marker_matches_case_sensitive);

  g_test_add_func ("/markdown_editor/insert_under_todo", test_insert_under_todo_lands_between_headers);
  g_test_add_func ("/markdown_editor/insert_under_reviews_occurrence_1", test_insert_under_reviews_occurrence_1_targets_development);
  g_test_add_func ("/markdown_editor/insert_under_reviews_occurrence_2", test_insert_under_reviews_occurrence_2_targets_operational);
  g_test_add_func ("/markdown_editor/insert_occurrence_beyond_available", test_insert_occurrence_beyond_available_matches_falls_back);
  g_test_add_func ("/markdown_editor/insert_occurrence_zero_is_one", test_insert_occurrence_zero_behaves_like_one);
  g_test_add_func ("/markdown_editor/insert_marker_not_found_fallback", test_insert_marker_not_found_falls_back_to_append);
  g_test_add_func ("/markdown_editor/insert_preserves_crlf", test_insert_preserves_crlf);
  g_test_add_func ("/markdown_editor/insert_multiline_filters_blanks", test_insert_multiline_filters_blanks_and_preserves_order);

  g_test_add_func ("/entry_writer/ensure_file_exists", test_entry_writer_ensure_file_exists_creates_file_and_dirs);
  g_test_add_func ("/entry_writer/add_entries_creates_then_inserts", test_entry_writer_add_entries_creates_then_inserts);

  g_test_add_func ("/config/round_trip", test_config_round_trip);
  g_test_add_func ("/config/missing_optional_fields", test_config_missing_optional_fields_load_cleanly);
  g_test_add_func ("/config/malformed_backs_up", test_config_malformed_file_backs_up_and_returns_defaults);

  return g_test_run ();
}
