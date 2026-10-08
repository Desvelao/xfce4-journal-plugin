#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <glib.h>

/* Replaces every literal "{TOKEN}" in `text` whose TOKEN name is a key in
 * `tokens` (GHashTable<const gchar*, const gchar*>, case-sensitive) with its
 * value. A "{TOKEN}" whose name is not a key in `tokens` is left untouched
 * verbatim — this lets a marker or template safely contain a literal "{"
 * unrelated to substitution (e.g. a code fence with braces). Single
 * left-to-right scan, no recursive/nested substitution. Caller owns the
 * returned string. */
gchar *template_render (const gchar *text, GHashTable *tokens);

#endif
