#include "template.h"

#include <string.h>

gchar *
template_render (const gchar *text, GHashTable *tokens)
{
  if (!text)
    return g_strdup ("");

  GString *out = g_string_new (NULL);
  const gchar *p = text;

  while (*p)
    {
      if (*p != '{')
        {
          g_string_append_c (out, *p);
          p++;
          continue;
        }

      const gchar *close = strchr (p + 1, '}');
      if (!close)
        {
          /* No matching '}' for the rest of the string: copy the '{' itself
           * and keep scanning — never loses trailing text. */
          g_string_append_c (out, *p);
          p++;
          continue;
        }

      gchar *name = g_strndup (p + 1, (gsize) (close - (p + 1)));
      gpointer value = tokens ? g_hash_table_lookup (tokens, name) : NULL;

      if (value)
        {
          g_string_append (out, (const gchar *) value);
          p = close + 1;
        }
      else
        {
          /* Unknown token name: leave "{...}" verbatim and advance past just
           * the '{' so a "{" that never resolves can't cause an infinite
           * loop or swallow unrelated text. */
          g_string_append_c (out, *p);
          p++;
        }

      g_free (name);
    }

  return g_string_free (out, FALSE);
}
