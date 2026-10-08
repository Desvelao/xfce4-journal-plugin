#include "config.h"

#include <jansson.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_SCHEMA_VERSION 1

gchar *
config_file_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "xfce4-journal-plugin", "config.json", NULL);
}

Category *
category_new (void)
{
  Category *category = g_new0 (Category, 1);
  category->marker_occurrence = 1;
  return category;
}

void
category_free (Category *category)
{
  if (!category)
    return;
  g_free (category->id);
  g_free (category->label);
  g_free (category->marker);
  g_free (category->entry_template);
  g_free (category);
}

Category *
category_copy (const Category *src)
{
  if (!src)
    return NULL;
  Category *dst = category_new ();
  dst->id = g_strdup (src->id);
  dst->label = g_strdup (src->label);
  dst->marker = g_strdup (src->marker);
  dst->marker_occurrence = src->marker_occurrence;
  dst->entry_type = src->entry_type;
  dst->entry_template = g_strdup (src->entry_template);
  return dst;
}

Journal *
journal_new (void)
{
  return g_new0 (Journal, 1);
}

void
journal_free (Journal *journal)
{
  if (!journal)
    return;
  g_free (journal->id);
  g_free (journal->name);
  g_free (journal->root_dir);
  g_free (journal->layout_template);
  g_free (journal->author);
  g_free (journal->new_file_template);
  g_list_free_full (journal->categories, (GDestroyNotify) category_free);
  g_free (journal);
}

Journal *
journal_copy (const Journal *src)
{
  if (!src)
    return NULL;
  Journal *dst = journal_new ();
  dst->id = g_strdup (src->id);
  dst->name = g_strdup (src->name);
  dst->root_dir = g_strdup (src->root_dir);
  dst->layout_template = g_strdup (src->layout_template);
  dst->author = g_strdup (src->author);
  dst->new_file_template = g_strdup (src->new_file_template);
  for (GList *l = src->categories; l != NULL; l = l->next)
    dst->categories = g_list_append (dst->categories, category_copy ((Category *) l->data));
  return dst;
}

Journal *
config_find_journal (const Config *cfg, const gchar *id)
{
  if (!cfg || !id)
    return NULL;
  for (GList *l = cfg->journals; l != NULL; l = l->next)
    {
      Journal *journal = l->data;
      if (g_strcmp0 (journal->id, id) == 0)
        return journal;
    }
  return NULL;
}

Journal *
config_find_journal_by_name (const Config *cfg, const gchar *name)
{
  if (!cfg || !name)
    return NULL;
  for (GList *l = cfg->journals; l != NULL; l = l->next)
    {
      Journal *journal = l->data;
      if (g_strcmp0 (journal->name, name) == 0)
        return journal;
    }
  return NULL;
}

Category *
journal_find_category (const Journal *journal, const gchar *id)
{
  if (!journal || !id)
    return NULL;
  for (GList *l = journal->categories; l != NULL; l = l->next)
    {
      Category *category = l->data;
      if (g_strcmp0 (category->id, id) == 0)
        return category;
    }
  return NULL;
}

static Config *
config_new_default (void)
{
  Config *cfg = g_new0 (Config, 1);
  cfg->config_schema_version = CONFIG_SCHEMA_VERSION;
  cfg->journals = NULL;
  return cfg;
}

void
config_free (Config *cfg)
{
  if (!cfg)
    return;
  g_list_free_full (cfg->journals, (GDestroyNotify) journal_free);
  g_free (cfg);
}

static Category *
category_from_json (json_t *obj)
{
  if (!json_is_object (obj))
    return NULL;

  json_t *id = json_object_get (obj, "id");
  json_t *marker = json_object_get (obj, "marker");

  if (!json_is_string (id) || !json_is_string (marker))
    return NULL;

  Category *category = category_new ();
  category->id = g_strdup (json_string_value (id));
  category->marker = g_strdup (json_string_value (marker));

  json_t *label = json_object_get (obj, "label");
  category->label = g_strdup (json_is_string (label) ? json_string_value (label) : "");

  json_t *marker_occurrence = json_object_get (obj, "marker_occurrence");
  category->marker_occurrence = (json_is_integer (marker_occurrence) && json_integer_value (marker_occurrence) >= 1)
    ? (gint) json_integer_value (marker_occurrence) : 1;

  json_t *entry_type = json_object_get (obj, "entry_type");
  json_t *entry_template = json_object_get (obj, "entry_template");
  const gchar *type_str = json_is_string (entry_type) ? json_string_value (entry_type) : "bullet";
  if (g_strcmp0 (type_str, "todo") == 0)
    category->entry_type = ENTRY_TYPE_TODO;
  else if (g_strcmp0 (type_str, "custom") == 0 && json_is_string (entry_template)
           && strstr (json_string_value (entry_template), ENTRY_PLACEHOLDER) != NULL)
    {
      category->entry_type = ENTRY_TYPE_CUSTOM;
      category->entry_template = g_strdup (json_string_value (entry_template));
    }
  else
    category->entry_type = ENTRY_TYPE_BULLET;

  return category;
}

static json_t *
category_to_json (const Category *category)
{
  json_t *obj = json_object ();
  json_object_set_new (obj, "id", json_string (category->id ? category->id : ""));
  json_object_set_new (obj, "label", json_string (category->label ? category->label : ""));
  json_object_set_new (obj, "marker", json_string (category->marker ? category->marker : ""));
  json_object_set_new (obj, "marker_occurrence", json_integer (category->marker_occurrence >= 1 ? category->marker_occurrence : 1));
  switch (category->entry_type)
    {
    case ENTRY_TYPE_TODO:
      json_object_set_new (obj, "entry_type", json_string ("todo"));
      break;
    case ENTRY_TYPE_CUSTOM:
      json_object_set_new (obj, "entry_type", json_string ("custom"));
      json_object_set_new (obj, "entry_template", json_string (category->entry_template ? category->entry_template : ""));
      break;
    default:
      json_object_set_new (obj, "entry_type", json_string ("bullet"));
      break;
    }
  return obj;
}

static Journal *
journal_from_json (json_t *obj)
{
  if (!json_is_object (obj))
    return NULL;

  json_t *id = json_object_get (obj, "id");
  json_t *name = json_object_get (obj, "name");
  json_t *root_dir = json_object_get (obj, "root_dir");

  if (!json_is_string (id) || !json_is_string (name) || !json_is_string (root_dir))
    return NULL;

  Journal *journal = journal_new ();
  journal->id = g_strdup (json_string_value (id));
  journal->name = g_strdup (json_string_value (name));
  journal->root_dir = g_strdup (json_string_value (root_dir));

  json_t *layout_template = json_object_get (obj, "layout_template");
  journal->layout_template = g_strdup (json_is_string (layout_template)
                                        ? json_string_value (layout_template) : LAYOUT_DEFAULT_TEMPLATE);

  json_t *author = json_object_get (obj, "author");
  journal->author = g_strdup (json_is_string (author) ? json_string_value (author) : "");

  json_t *new_file_template = json_object_get (obj, "new_file_template");
  journal->new_file_template = g_strdup (json_is_string (new_file_template)
                                          ? json_string_value (new_file_template) : "");

  json_t *categories = json_object_get (obj, "categories");
  if (json_is_array (categories))
    {
      size_t idx;
      json_t *item;
      json_array_foreach (categories, idx, item)
        {
          Category *category = category_from_json (item);
          if (category)
            journal->categories = g_list_append (journal->categories, category);
        }
    }

  return journal;
}

static json_t *
journal_to_json (const Journal *journal)
{
  json_t *obj = json_object ();
  json_object_set_new (obj, "id", json_string (journal->id ? journal->id : ""));
  json_object_set_new (obj, "name", json_string (journal->name ? journal->name : ""));
  json_object_set_new (obj, "root_dir", json_string (journal->root_dir ? journal->root_dir : ""));
  json_object_set_new (obj, "layout_template",
                        json_string (journal->layout_template ? journal->layout_template : LAYOUT_DEFAULT_TEMPLATE));
  json_object_set_new (obj, "author", json_string (journal->author ? journal->author : ""));
  json_object_set_new (obj, "new_file_template",
                        json_string (journal->new_file_template ? journal->new_file_template : ""));

  json_t *categories = json_array ();
  for (GList *l = journal->categories; l != NULL; l = l->next)
    json_array_append_new (categories, category_to_json ((Category *) l->data));
  json_object_set_new (obj, "categories", categories);

  return obj;
}

static void
backup_bad_file (const gchar *path)
{
  gchar *backup_path = g_strdup_printf ("%s.bak", path);
  g_rename (path, backup_path);
  g_free (backup_path);
}

Config *
config_load_from_path (const gchar *path)
{
  if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
    return config_new_default ();

  json_error_t jerr;
  json_t *root = json_load_file (path, 0, &jerr);

  if (!root || !json_is_object (root))
    {
      if (root)
        json_decref (root);
      g_warning ("xfce4-journal-plugin: malformed config at %s, backing up and using defaults", path);
      backup_bad_file (path);
      return config_new_default ();
    }

  Config *cfg = config_new_default ();

  json_t *version = json_object_get (root, "config_schema_version");
  if (json_is_integer (version))
    cfg->config_schema_version = (gint) json_integer_value (version);

  json_t *journals = json_object_get (root, "journals");
  if (json_is_array (journals))
    {
      size_t idx;
      json_t *item;
      json_array_foreach (journals, idx, item)
        {
          Journal *journal = journal_from_json (item);
          if (journal)
            cfg->journals = g_list_append (cfg->journals, journal);
        }
    }

  json_decref (root);
  return cfg;
}

Config *
config_load (void)
{
  gchar *path = config_file_path ();
  Config *cfg = config_load_from_path (path);
  g_free (path);
  return cfg;
}

gboolean
config_save_to_path (const Config *cfg, const gchar *path, GError **error)
{
  gchar *dir = g_path_get_dirname (path);

  if (g_mkdir_with_parents (dir, 0700) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "Could not create config directory %s", dir);
      g_free (dir);
      return FALSE;
    }
  g_free (dir);

  json_t *root = json_object ();
  json_object_set_new (root, "config_schema_version", json_integer (CONFIG_SCHEMA_VERSION));

  json_t *journals = json_array ();
  for (GList *l = cfg->journals; l != NULL; l = l->next)
    json_array_append_new (journals, journal_to_json ((Journal *) l->data));
  json_object_set_new (root, "journals", journals);

  gchar *serialized = json_dumps (root, JSON_INDENT (2));
  json_decref (root);

  if (!serialized)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Failed to serialize config");
      return FALSE;
    }

  gchar *tmp_path = g_strdup_printf ("%s.tmp.%d", path, getpid ());

  GError *local_error = NULL;
  gboolean wrote = g_file_set_contents (tmp_path, serialized, -1, &local_error);
  free (serialized); /* json_dumps uses the C library allocator, not g_malloc */

  if (!wrote)
    {
      g_propagate_error (error, local_error);
      g_free (tmp_path);
      return FALSE;
    }

  if (g_chmod (tmp_path, 0600) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "Could not chmod config file");
      g_unlink (tmp_path);
      g_free (tmp_path);
      return FALSE;
    }

  if (g_rename (tmp_path, path) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "Could not replace config file");
      g_unlink (tmp_path);
      g_free (tmp_path);
      return FALSE;
    }

  g_free (tmp_path);
  return TRUE;
}

gboolean
config_save (const Config *cfg, GError **error)
{
  gchar *path = config_file_path ();
  gboolean ok = config_save_to_path (cfg, path, error);
  g_free (path);
  return ok;
}
