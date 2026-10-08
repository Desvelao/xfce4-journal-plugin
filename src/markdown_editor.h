#ifndef MARKDOWN_EDITOR_H
#define MARKDOWN_EDITOR_H

#include <glib.h>
#include "config.h"

typedef enum {
  MARKDOWN_LINE_ENDING_LF,
  MARKDOWN_LINE_ENDING_CRLF,
} MarkdownLineEnding;

/* Returns MARKDOWN_LINE_ENDING_CRLF if `content` contains at least one
 * "\r\n" sequence (matches a journal's historical CRLF convention even if a
 * few lines are LF-only), else MARKDOWN_LINE_ENDING_LF. An empty/new file
 * defaults to LF. Used so re-inserting content never silently rewrites a
 * CRLF file's *untouched* lines to LF — only the bytes actually spliced in
 * are new, keeping any git diff of the journal minimal. */
MarkdownLineEnding markdown_editor_detect_line_ending (const gchar *content);

/* Splits `content` on "\n" into a NULL-terminated array (GStrv, free with
 * g_strfreev), stripping any trailing '\r' from each resulting line —
 * tolerates both "\n" and "\r\n" without the caller needing to know which
 * the file uses. The chosen line-ending style is NOT retained per-line;
 * markdown_editor_join_lines() always re-applies one uniform style. */
gchar **markdown_editor_split_lines (const gchar *content);

/* Re-joins `lines` (NULL-terminated) with `ending` between every pair of
 * adjacent lines (no trailing terminator added after the last line — callers
 * that want a final newline append one themselves). Caller owns the
 * returned string. */
gchar *markdown_editor_join_lines (gchar * const *lines, MarkdownLineEnding ending);

/* TRUE if `line` (a single line, no trailing '\n'/'\r') "looks like a
 * header" per the style-agnostic rule bounding a category's body: an ATX
 * heading "^#{1,6}\s" (1-6 '#' at column 0 followed by whitespace) OR a
 * bold-only line "^\*{1,2}[^*\n]+\*{1,2}\s*$" wrapping the WHOLE trimmed
 * line in matching single or double asterisks (e.g. "*Reviews*",
 * "**Reviews**"). Leading whitespace is NOT stripped before the check (a
 * header is expected to start at column 0); trailing whitespace IS
 * ignored. */
gboolean markdown_editor_line_looks_like_header (const gchar *line);

/* TRUE if `line`, after stripping a trailing '\r' and g_strstrip()-ing both
 * `line` and `marker`, is byte-identical to `marker`. */
gboolean markdown_editor_line_matches_marker (const gchar *line, const gchar *marker);

typedef struct {
  gboolean marker_found;
  gint insert_index;  /* index into a GStrv (as from markdown_editor_split_lines)
                          at which new lines should be inserted (before the
                          element currently at that index, or at EOF if
                          insert_index == g_strv_length(lines)). When
                          marker_found is FALSE this always equals
                          g_strv_length(lines) — the fallback case, see
                          markdown_editor_insert_entries(). */
} MarkdownInsertionPoint;

/* Scans `lines` for the `occurrence`-th (1-based; values < 1 treated as 1)
 * line matching `marker` (markdown_editor_line_matches_marker); if that many
 * matches exist, scans forward from the line AFTER the matched one for the
 * first line where markdown_editor_line_looks_like_header() is TRUE, and
 * sets insert_index to that line's index (or g_strv_length(lines) if no such
 * line follows — the marker's section runs to end-of-file). If `lines`
 * contains fewer than `occurrence` lines matching `marker` (including zero),
 * marker_found is FALSE — same fallback as a marker that isn't present at
 * all, see markdown_editor_insert_entries(). occurrence=1 is the original,
 * still-default behavior: the first match. A repeated marker (e.g.
 * "*Reviews*" appearing once under "*Development*" and again under
 * "*Operational*") can be targeted precisely by two categories using
 * occurrence 1 and 2 respectively — see docs/CONFIGURATION.md. */
MarkdownInsertionPoint markdown_editor_find_insertion_point (gchar * const *lines, const gchar *marker, gint occurrence);

/* The core "add an entry" transform. `original_content` is a day file's
 * full text (or "" for a brand-new file). `marker` is the category's exact
 * header line; `occurrence` selects which matching line to target when
 * `marker` appears more than once (see markdown_editor_find_insertion_point;
 * 1 = first match, the default). `entry_lines` (NULL-terminated) becomes one
 * formatted line (see markdown_editor_format_entry: bullet, todo or custom
 * template) per non-blank (after g_strstrip) element, in the order given —
 * blank/whitespace-only elements are silently skipped (never produce an
 * empty "- " bullet).
 *
 * If the requested occurrence is found: the bullets are inserted immediately
 * before the next header-like line following it (or at EOF if none),
 * preserving every other line untouched.
 *
 * If it's NOT found — including when `marker` appears fewer times than
 * `occurrence` requests — (fallback): the marker line, then the new
 * bullets, are appended at the very end of the file — preceded by one
 * blank line if `original_content` is non-empty. This is deliberately
 * visible/inspectable (the user immediately sees a plainly-labeled new
 * section they can move) rather than silently discarding the entry or
 * refusing to add it.
 *
 * The output uses whichever line ending markdown_editor_detect_line_ending()
 * reports for `original_content` (LF for a new/empty file) throughout.
 * Caller owns the returned string. */
gchar *markdown_editor_insert_entries (const gchar *original_content,
                                        const gchar *marker,
                                        gint occurrence,
                                        EntryType entry_type,
                                        const gchar *entry_template,
                                        gchar * const *entry_lines);

/* Formats one already-trimmed entry: "- text" (bullet), "- [ ] text" (todo),
 * or, for custom, `entry_template` with every ENTRY_PLACEHOLDER ("$ENTRY")
 * replaced by `text`. A custom type whose template is NULL or lacks the
 * placeholder falls back to bullet. Caller owns the returned string. */
gchar *markdown_editor_format_entry (EntryType entry_type, const gchar *entry_template, const gchar *text);

#endif
