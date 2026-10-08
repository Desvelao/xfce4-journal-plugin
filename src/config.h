#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>

#define LAYOUT_DEFAULT_TEMPLATE "{YYYY}/{MM}/{YYYY}-{MM}-{DD}.md"

typedef enum {
  ENTRY_TYPE_BULLET = 0, /* "- text" (default) */
  ENTRY_TYPE_TODO,       /* "- [ ] text" */
  ENTRY_TYPE_CUSTOM      /* user template; every "$ENTRY" is replaced by the text */
} EntryType;

#define ENTRY_PLACEHOLDER "$ENTRY"

typedef struct {
  gchar *id;      /* stable internal id (g_uuid_string_random), never shown to the user */
  gchar *label;   /* nullable/empty; shown in the Add-entry menu. Falls back to displaying
                      `marker` itself when empty. */
  gchar *marker;  /* required; the EXACT header line as it appears in a day file, whatever
                      its style, e.g. "## TODO", "### On hold", "*Reviews*". Matched verbatim
                      (after trimming/\r-stripping) by markdown_editor_line_matches_marker(). */
  gint marker_occurrence; /* 1-based; which match of `marker` to target when it appears more
                              than once in a day file (e.g. 2 for the second "*Reviews*", under
                              "*Operational*" rather than "*Development*"). Default/minimum 1
                              (the first match) — see markdown_editor_find_insertion_point(). */
  EntryType entry_type;   /* how each new entry line is written; default ENTRY_TYPE_BULLET */
  gchar *entry_template;  /* only meaningful for ENTRY_TYPE_CUSTOM; must contain ENTRY_PLACEHOLDER */
} Category;

typedef struct {
  gchar *id;                 /* stable internal id (g_uuid_string_random) */
  gchar *name;                /* unique, non-empty; user-facing label */
  gchar *root_dir;             /* absolute path; required */
  gchar *layout_template;      /* required; default LAYOUT_DEFAULT_TEMPLATE. Rendered against a
                                   date via layout_render_relative_path() to get a day file's
                                   path relative to root_dir. */
  gchar *author;                /* nullable/empty; substituted into a new file's {AUTHOR} token */
  gchar *new_file_template;     /* nullable/empty; raw text with {DATE}/{AUTHOR}/{YYYY}/{MM}/{DD}
                                    placeholders, written verbatim (via template_render()) to
                                    create a day file that doesn't exist yet. Authored
                                    independently of `categories` below — see
                                    docs/CONFIGURATION.md for how the two interact. */
  GList *categories;            /* ordered list of owned Category*; menu order == this order */
} Journal;

typedef struct {
  gint config_schema_version;
  GList *journals;   /* ordered list of owned Journal* */
} Config;

/* ~/.config/xfce4-journal-plugin/config.json. Caller owns the returned string. */
gchar *config_file_path (void);

/* Missing file -> in-memory defaults (not an error). Malformed file ->
 * renamed to <path>.bak, defaults returned (never crashes). */
Config *config_load (void);

/* Atomic write: serialize -> <path>.tmp.<pid> -> chmod 0600 -> rename. */
gboolean config_save (const Config *cfg, GError **error);

void config_free (Config *cfg);

/* Path-injectable variants config_load/config_save thinly wrap, so tests can
 * round-trip against a temp path without touching the real ~/.config. */
Config *config_load_from_path (const gchar *path);
gboolean config_save_to_path (const Config *cfg, const gchar *path, GError **error);

Journal *journal_new (void);
void journal_free (Journal *journal);
Journal *journal_copy (const Journal *journal); /* deep-copies categories */

Category *category_new (void);
void category_free (Category *category);
Category *category_copy (const Category *category);

/* Linear scans. Return NULL if not found or id/name is NULL. */
Journal *config_find_journal (const Config *cfg, const gchar *id);
Journal *config_find_journal_by_name (const Config *cfg, const gchar *name);
Category *journal_find_category (const Journal *journal, const gchar *id);

#endif
