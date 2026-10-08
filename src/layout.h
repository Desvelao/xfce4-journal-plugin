#ifndef LAYOUT_H
#define LAYOUT_H

#include <glib.h>
#include "config.h"

/* Renders `layout_template` against `date` using tokens {YYYY} (4-digit),
 * {MM} (2-digit, zero-padded), {DD} (2-digit, zero-padded) via
 * template_render() — pure, no filesystem access, so a flat layout like
 * "{YYYY}-{MM}-{DD}.md" is exercised identically to the nested default.
 * Caller owns the returned string. */
gchar *layout_render_relative_path (const gchar *layout_template, GDateTime *date);

/* g_build_filename (journal->root_dir, <rendered relative path>, NULL).
 * Caller owns the returned string. */
gchar *layout_absolute_path_for_date (const Journal *journal, GDateTime *date);

/* layout_absolute_path_for_date() for the local-calendar "today"
 * (g_date_time_new_now_local()). Caller owns the returned string. */
gchar *layout_today_absolute_path (const Journal *journal);

/* Renders journal->new_file_template (NULL/"" is valid — an empty template
 * means a freshly created day file starts out genuinely empty, not an
 * error) via template_render() with tokens {DATE} ("YYYY-MM-DD"), {AUTHOR}
 * (journal->author, or "" if unset), {YYYY}, {MM}, {DD}. Caller owns the
 * returned string. */
gchar *layout_render_new_file_content (const Journal *journal, GDateTime *date);

#endif
