#include "preferences.h"
#include <string.h>
#include "config.h"
#include "template.h"

enum { COL_J_ID, COL_J_NAME, COL_J_ROOT, COL_J_CATEGORY_COUNT, N_J_COLS };
enum { COL_C_ID, COL_C_LABEL, COL_C_MARKER, N_C_COLS };

typedef struct {
  Config *cfg;
  PanelWidget *panel_widget;
  GtkListStore *store;
  GtkWidget *tree_view;
} PrefsContext;

/* Local, in-dialog-only state for a journal editor's nested categories
 * list — never touches disk; only materializes into a real Journal* on the
 * journal editor's own OK (see run_journal_editor_dialog). */
typedef struct {
  GList *categories; /* working GList of owned Category*, mutated live */
  GtkListStore *store;
  GtkWidget *tree_view;
} CategoriesEditor;

typedef struct {
  GtkWidget *dialog;
  GtkWidget *entry;
} ValidateCtx;

typedef struct {
  GtkWidget *dialog;
  GtkWidget *name_entry;
  GtkWidget *root_entry;
} JournalValidateCtx;

typedef struct {
  GtkTextBuffer *template_buffer;
  GtkWidget *author_entry;
  GtkTextBuffer *preview_buffer;
} TemplatePreviewCtx;

static void refresh_journal_store (PrefsContext *ctx);
static void save_and_apply (PrefsContext *ctx);
static gchar *get_selected_journal_id (PrefsContext *ctx);
static GList *find_journal_node (Config *cfg, const gchar *id);
static void show_error (PrefsContext *ctx, const gchar *message);
static void swap_journal_positions (GList *a, GList *b);
static void select_journal_row (PrefsContext *ctx, const gchar *journal_id);

static void categories_editor_refresh_store (CategoriesEditor *editor);
static gchar *categories_editor_get_selected_id (CategoriesEditor *editor);
static GList *categories_editor_find_node (CategoriesEditor *editor, const gchar *id);
static void categories_editor_select_row (CategoriesEditor *editor, const gchar *category_id);

static Category *run_category_editor_dialog (GtkWindow *parent, const Category *existing);
static void on_add_category_clicked (GtkButton *button, gpointer user_data);
static void on_edit_category_clicked (GtkButton *button, gpointer user_data);
static void on_remove_category_clicked (GtkButton *button, gpointer user_data);
static void on_move_category_up_clicked (GtkButton *button, gpointer user_data);
static void on_move_category_down_clicked (GtkButton *button, gpointer user_data);

static void on_browse_root_dir_clicked (GtkButton *button, gpointer user_data);
static void update_template_preview (TemplatePreviewCtx *ctx);
static void on_template_preview_buffer_changed (GtkTextBuffer *buffer, gpointer user_data);
static void on_template_preview_author_changed (GtkEditable *editable, gpointer user_data);
static Journal *run_journal_editor_dialog (GtkWindow *parent, const Journal *existing);
static void on_add_journal_clicked (GtkButton *button, gpointer user_data);
static void on_edit_journal_clicked (GtkButton *button, gpointer user_data);
static void on_remove_journal_clicked (GtkButton *button, gpointer user_data);
static void on_move_journal_up_clicked (GtkButton *button, gpointer user_data);
static void on_move_journal_down_clicked (GtkButton *button, gpointer user_data);

static void open_uri_or_show_error (GtkWidget *relative_widget, const gchar *uri, const gchar *what);
static void on_open_config_file_clicked (GtkButton *button, gpointer user_data);
static void on_open_config_dir_clicked (GtkButton *button, gpointer user_data);
static void build_preferences_contents (GtkDialog *dialog, PanelWidget *panel_widget);
static void on_dialog_response (GtkDialog *dialog, gint response_id, gpointer user_data);

static void
prefs_context_free (gpointer data)
{
  PrefsContext *ctx = data;
  config_free (ctx->cfg);
  g_free (ctx);
}

static gchar *
get_selected_journal_id (PrefsContext *ctx)
{
  GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (ctx->tree_view));
  GtkTreeModel *model;
  GtkTreeIter iter;
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return NULL;
  gchar *id = NULL;
  gtk_tree_model_get (model, &iter, COL_J_ID, &id, -1);
  return id;
}

static GList *
find_journal_node (Config *cfg, const gchar *id)
{
  for (GList *l = cfg->journals; l != NULL; l = l->next)
    {
      Journal *journal = l->data;
      if (g_strcmp0 (journal->id, id) == 0)
        return l;
    }
  return NULL;
}

static void
show_error (PrefsContext *ctx, const gchar *message)
{
  GtkWidget *dialog = gtk_message_dialog_new (
      GTK_WINDOW (gtk_widget_get_toplevel (ctx->tree_view)), GTK_DIALOG_MODAL,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
refresh_journal_store (PrefsContext *ctx)
{
  gtk_list_store_clear (ctx->store);
  for (GList *l = ctx->cfg->journals; l != NULL; l = l->next)
    {
      Journal *journal = l->data;
      gchar *count_text = g_strdup_printf ("%u", g_list_length (journal->categories));
      GtkTreeIter iter;
      gtk_list_store_append (ctx->store, &iter);
      gtk_list_store_set (ctx->store, &iter,
                           COL_J_ID, journal->id,
                           COL_J_NAME, journal->name,
                           COL_J_ROOT, journal->root_dir,
                           COL_J_CATEGORY_COUNT, count_text,
                           -1);
      g_free (count_text);
    }
}

static void
save_and_apply (PrefsContext *ctx)
{
  GError *error = NULL;
  if (!config_save (ctx->cfg, &error))
    {
      GtkWidget *dialog = gtk_message_dialog_new (
          GTK_WINDOW (gtk_widget_get_toplevel (ctx->tree_view)), GTK_DIALOG_MODAL,
          GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Failed to save configuration: %s",
          error ? error->message : "unknown error");
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      g_clear_error (&error);
    }
  panel_widget_reload_config (ctx->panel_widget);
  refresh_journal_store (ctx);
}

/* Reorders by swapping the Journal pointers held by two adjacent list
 * cells — simpler and less error-prone than unlinking/reinserting GList
 * nodes, and the list order is exactly what config_save/config_load
 * persist/restore. */
static void
swap_journal_positions (GList *a, GList *b)
{
  gpointer tmp = a->data;
  a->data = b->data;
  b->data = tmp;
}

static void
select_journal_row (PrefsContext *ctx, const gchar *journal_id)
{
  if (!journal_id)
    return;
  GtkTreeIter iter;
  gboolean valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (ctx->store), &iter);
  while (valid)
    {
      gchar *id = NULL;
      gtk_tree_model_get (GTK_TREE_MODEL (ctx->store), &iter, COL_J_ID, &id, -1);
      gboolean match = g_strcmp0 (id, journal_id) == 0;
      g_free (id);
      if (match)
        {
          gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (ctx->tree_view)), &iter);
          return;
        }
      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (ctx->store), &iter);
    }
}

static void
categories_editor_refresh_store (CategoriesEditor *editor)
{
  gtk_list_store_clear (editor->store);
  for (GList *l = editor->categories; l != NULL; l = l->next)
    {
      Category *category = l->data;
      gchar *marker_text = (category->marker_occurrence > 1)
        ? g_strdup_printf ("%s (match #%d)", category->marker, category->marker_occurrence)
        : g_strdup (category->marker);
      GtkTreeIter iter;
      gtk_list_store_append (editor->store, &iter);
      gtk_list_store_set (editor->store, &iter,
                           COL_C_ID, category->id,
                           COL_C_LABEL, (category->label && *category->label) ? category->label : "",
                           COL_C_MARKER, marker_text,
                           -1);
      g_free (marker_text);
    }
}

static gchar *
categories_editor_get_selected_id (CategoriesEditor *editor)
{
  GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (editor->tree_view));
  GtkTreeModel *model;
  GtkTreeIter iter;
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return NULL;
  gchar *id = NULL;
  gtk_tree_model_get (model, &iter, COL_C_ID, &id, -1);
  return id;
}

static GList *
categories_editor_find_node (CategoriesEditor *editor, const gchar *id)
{
  for (GList *l = editor->categories; l != NULL; l = l->next)
    {
      Category *category = l->data;
      if (g_strcmp0 (category->id, id) == 0)
        return l;
    }
  return NULL;
}

static void
categories_editor_select_row (CategoriesEditor *editor, const gchar *category_id)
{
  if (!category_id)
    return;
  GtkTreeIter iter;
  gboolean valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (editor->store), &iter);
  while (valid)
    {
      gchar *id = NULL;
      gtk_tree_model_get (GTK_TREE_MODEL (editor->store), &iter, COL_C_ID, &id, -1);
      gboolean match = g_strcmp0 (id, category_id) == 0;
      g_free (id);
      if (match)
        {
          gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (editor->tree_view)), &iter);
          return;
        }
      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (editor->store), &iter);
    }
}

static void
validate_entry_required (GtkEditable *editable, gpointer user_data)
{
  (void) editable;
  ValidateCtx *v = user_data;
  const gchar *text = gtk_entry_get_text (GTK_ENTRY (v->entry));
  gboolean ok = text && *text;
  gtk_dialog_set_response_sensitive (GTK_DIALOG (v->dialog), GTK_RESPONSE_OK, ok);
}

static void
validate_journal_editor (GtkEditable *editable, gpointer user_data)
{
  (void) editable;
  JournalValidateCtx *v = user_data;
  const gchar *name = gtk_entry_get_text (GTK_ENTRY (v->name_entry));
  const gchar *root = gtk_entry_get_text (GTK_ENTRY (v->root_entry));
  gboolean ok = name && *name && root && *root;
  gtk_dialog_set_response_sensitive (GTK_DIALOG (v->dialog), GTK_RESPONSE_OK, ok);
}

typedef struct {
  GtkWidget *dialog;
  GtkWidget *marker_entry;
  GtkWidget *type_combo;
  GtkWidget *template_entry;
} CategoryValidateCtx;

static void
validate_category_dialog (GtkWidget *widget, gpointer user_data)
{
  (void) widget;
  CategoryValidateCtx *v = user_data;
  const gchar *marker = gtk_entry_get_text (GTK_ENTRY (v->marker_entry));
  gboolean custom = gtk_combo_box_get_active (GTK_COMBO_BOX (v->type_combo)) == ENTRY_TYPE_CUSTOM;
  gtk_widget_set_sensitive (v->template_entry, custom);
  gboolean ok = marker && *marker;
  if (ok && custom)
    ok = strstr (gtk_entry_get_text (GTK_ENTRY (v->template_entry)), ENTRY_PLACEHOLDER) != NULL;
  gtk_dialog_set_response_sensitive (GTK_DIALOG (v->dialog), GTK_RESPONSE_OK, ok);
}

static Category *
run_category_editor_dialog (GtkWindow *parent, const Category *existing)
{
  gboolean editing = (existing != NULL);
  GtkWidget *dialog = gtk_dialog_new_with_buttons (
      editing ? "Edit Category" : "Add Category", parent, GTK_DIALOG_MODAL,
      "_Cancel", GTK_RESPONSE_CANCEL, editing ? "_Save" : "_Add", GTK_RESPONSE_OK, NULL);

  GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  GtkWidget *grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
  gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
  gtk_container_add (GTK_CONTAINER (content), grid);

  GtkWidget *marker_entry = gtk_entry_new ();
  if (existing)
    gtk_entry_set_text (GTK_ENTRY (marker_entry), existing->marker);
  gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("Marker:"), 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), marker_entry, 1, 0, 1, 1);

  GtkWidget *marker_hint = gtk_label_new (
      "Exact header line as it appears in your day files — e.g. \"## TODO\" or "
      "\"*Reviews*\" — matched exactly (after trimming) to find where new entries go.");
  gtk_label_set_line_wrap (GTK_LABEL (marker_hint), TRUE);
  gtk_label_set_line_wrap_mode (GTK_LABEL (marker_hint), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars (GTK_LABEL (marker_hint), 64);
  gtk_label_set_xalign (GTK_LABEL (marker_hint), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (marker_hint), "dim-label");
  gtk_grid_attach (GTK_GRID (grid), marker_hint, 0, 1, 2, 1);

  GtkWidget *occurrence_spin = gtk_spin_button_new_with_range (1, 99, 1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (occurrence_spin),
                              (existing && existing->marker_occurrence >= 1) ? existing->marker_occurrence : 1);
  gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("Occurrence:"), 0, 2, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), occurrence_spin, 1, 2, 1, 1);

  GtkWidget *occurrence_hint = gtk_label_new (
      "Which matching header to use if the marker text appears more than once in a day's "
      "file — e.g. 2 to target the second \"*Reviews*\" (under \"*Operational*\" rather "
      "than \"*Development*\"). 1 = first match (default).");
  gtk_label_set_line_wrap (GTK_LABEL (occurrence_hint), TRUE);
  gtk_label_set_line_wrap_mode (GTK_LABEL (occurrence_hint), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars (GTK_LABEL (occurrence_hint), 64);
  gtk_label_set_xalign (GTK_LABEL (occurrence_hint), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (occurrence_hint), "dim-label");
  gtk_grid_attach (GTK_GRID (grid), occurrence_hint, 0, 3, 2, 1);

  GtkWidget *label_entry = gtk_entry_new ();
  if (existing && existing->label)
    gtk_entry_set_text (GTK_ENTRY (label_entry), existing->label);
  gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("Label:"), 0, 4, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), label_entry, 1, 4, 1, 1);

  GtkWidget *label_hint = gtk_label_new ("Shown in the Add-entry menu. Leave blank to show the marker text itself.");
  gtk_label_set_line_wrap (GTK_LABEL (label_hint), TRUE);
  gtk_label_set_line_wrap_mode (GTK_LABEL (label_hint), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars (GTK_LABEL (label_hint), 64);
  gtk_label_set_xalign (GTK_LABEL (label_hint), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (label_hint), "dim-label");
  gtk_grid_attach (GTK_GRID (grid), label_hint, 0, 5, 2, 1);

  GtkWidget *type_combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (type_combo), "Bullet  (- text)");
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (type_combo), "To-do  (- [ ] text)");
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (type_combo), "Custom");
  gtk_combo_box_set_active (GTK_COMBO_BOX (type_combo), existing ? (gint) existing->entry_type : ENTRY_TYPE_BULLET);
  gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("Entry type:"), 0, 6, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), type_combo, 1, 6, 1, 1);

  GtkWidget *template_entry = gtk_entry_new ();
  if (existing && existing->entry_template)
    gtk_entry_set_text (GTK_ENTRY (template_entry), existing->entry_template);
  gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("Template:"), 0, 7, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), template_entry, 1, 7, 1, 1);

  GtkWidget *template_hint = gtk_label_new (
      "Custom only: the line written for each entry, with $ENTRY marking where the text "
      "goes — e.g. \"* [ ] $ENTRY\" or \"- $ENTRY #work\". Must contain $ENTRY.");
  gtk_label_set_line_wrap (GTK_LABEL (template_hint), TRUE);
  gtk_label_set_line_wrap_mode (GTK_LABEL (template_hint), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars (GTK_LABEL (template_hint), 64);
  gtk_label_set_xalign (GTK_LABEL (template_hint), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (template_hint), "dim-label");
  gtk_grid_attach (GTK_GRID (grid), template_hint, 0, 8, 2, 1);

  CategoryValidateCtx vctx = { dialog, marker_entry, type_combo, template_entry };
  g_signal_connect (marker_entry, "changed", G_CALLBACK (validate_category_dialog), &vctx);
  g_signal_connect (template_entry, "changed", G_CALLBACK (validate_category_dialog), &vctx);
  g_signal_connect (type_combo, "changed", G_CALLBACK (validate_category_dialog), &vctx);
  validate_category_dialog (NULL, &vctx);

  gtk_widget_show_all (dialog);

  Category *result = NULL;
  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
      result = editing ? category_copy (existing) : category_new ();
      g_free (result->marker);
      result->marker = g_strdup (gtk_entry_get_text (GTK_ENTRY (marker_entry)));
      result->marker_occurrence = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (occurrence_spin));
      g_free (result->label);
      result->label = g_strdup (gtk_entry_get_text (GTK_ENTRY (label_entry)));
      result->entry_type = (EntryType) gtk_combo_box_get_active (GTK_COMBO_BOX (type_combo));
      g_free (result->entry_template);
      result->entry_template = (result->entry_type == ENTRY_TYPE_CUSTOM)
        ? g_strdup (gtk_entry_get_text (GTK_ENTRY (template_entry))) : NULL;
      if (!editing)
        result->id = g_uuid_string_random ();
    }

  gtk_widget_destroy (dialog);
  return result;
}

static void
on_add_category_clicked (GtkButton *button, gpointer user_data)
{
  CategoriesEditor *editor = user_data;
  Category *new_category = run_category_editor_dialog (
      GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))), NULL);
  if (!new_category)
    return;
  editor->categories = g_list_append (editor->categories, new_category);
  categories_editor_refresh_store (editor);
}

static void
on_edit_category_clicked (GtkButton *button, gpointer user_data)
{
  CategoriesEditor *editor = user_data;
  gchar *selected_id = categories_editor_get_selected_id (editor);
  if (!selected_id)
    return;

  GList *node = categories_editor_find_node (editor, selected_id);
  g_free (selected_id);
  if (!node)
    return;

  Category *existing = node->data;
  Category *edited = run_category_editor_dialog (
      GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))), existing);
  if (!edited)
    return;

  category_free (existing);
  node->data = edited;
  categories_editor_refresh_store (editor);
}

static void
on_remove_category_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  CategoriesEditor *editor = user_data;
  gchar *selected_id = categories_editor_get_selected_id (editor);
  if (!selected_id)
    return;

  GList *node = categories_editor_find_node (editor, selected_id);
  g_free (selected_id);
  if (!node)
    return;

  Category *category = node->data;
  editor->categories = g_list_delete_link (editor->categories, node);
  category_free (category);
  categories_editor_refresh_store (editor);
}

static void
swap_category_positions (GList *a, GList *b)
{
  gpointer tmp = a->data;
  a->data = b->data;
  b->data = tmp;
}

static void
on_move_category_up_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  CategoriesEditor *editor = user_data;
  gchar *selected_id = categories_editor_get_selected_id (editor);
  if (!selected_id)
    return;

  GList *node = categories_editor_find_node (editor, selected_id);
  if (node && node->prev)
    {
      swap_category_positions (node, node->prev);
      categories_editor_refresh_store (editor);
      categories_editor_select_row (editor, selected_id);
    }
  g_free (selected_id);
}

static void
on_move_category_down_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  CategoriesEditor *editor = user_data;
  gchar *selected_id = categories_editor_get_selected_id (editor);
  if (!selected_id)
    return;

  GList *node = categories_editor_find_node (editor, selected_id);
  if (node && node->next)
    {
      swap_category_positions (node, node->next);
      categories_editor_refresh_store (editor);
      categories_editor_select_row (editor, selected_id);
    }
  g_free (selected_id);
}

static void
on_browse_root_dir_clicked (GtkButton *button, gpointer user_data)
{
  GtkWidget *root_entry = GTK_WIDGET (user_data);
  GtkWidget *dialog = gtk_file_chooser_dialog_new (
      "Select root directory", GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);
  gtk_file_chooser_set_create_folders (GTK_FILE_CHOOSER (dialog), TRUE);

  const gchar *current = gtk_entry_get_text (GTK_ENTRY (root_entry));
  if (current && *current && g_file_test (current, G_FILE_TEST_IS_DIR))
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (dialog), current);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
      gchar *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      if (filename)
        {
          gtk_entry_set_text (GTK_ENTRY (root_entry), filename);
          g_free (filename);
        }
    }
  gtk_widget_destroy (dialog);
}

/* Renders ctx->template_buffer's current text against today's {DATE}/
 * {AUTHOR}/{YYYY}/{MM}/{DD} tokens (the same set layout_render_new_file_content
 * builds for a Journal) and writes the result into ctx->preview_buffer — the
 * editor works from raw entry/buffer text, not a Journal*, so the tokens are
 * built inline here rather than by calling that function directly. */
static void
update_template_preview (TemplatePreviewCtx *ctx)
{
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (ctx->template_buffer, &start, &end);
  gchar *template_text = gtk_text_buffer_get_text (ctx->template_buffer, &start, &end, FALSE);

  GDateTime *now = g_date_time_new_now_local ();
  GHashTable *tokens = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  g_hash_table_insert (tokens, "YYYY", g_strdup_printf ("%04d", g_date_time_get_year (now)));
  g_hash_table_insert (tokens, "MM", g_strdup_printf ("%02d", g_date_time_get_month (now)));
  g_hash_table_insert (tokens, "DD", g_strdup_printf ("%02d", g_date_time_get_day_of_month (now)));
  g_hash_table_insert (tokens, "DATE",
                        g_strdup_printf ("%04d-%02d-%02d", g_date_time_get_year (now),
                                          g_date_time_get_month (now), g_date_time_get_day_of_month (now)));
  g_hash_table_insert (tokens, "AUTHOR", g_strdup (gtk_entry_get_text (GTK_ENTRY (ctx->author_entry))));
  g_date_time_unref (now);

  gchar *rendered = template_render (template_text, tokens);
  g_hash_table_unref (tokens);
  g_free (template_text);

  gtk_text_buffer_set_text (ctx->preview_buffer, rendered, -1);
  g_free (rendered);
}

static void
on_template_preview_buffer_changed (GtkTextBuffer *buffer, gpointer user_data)
{
  (void) buffer;
  update_template_preview ((TemplatePreviewCtx *) user_data);
}

static void
on_template_preview_author_changed (GtkEditable *editable, gpointer user_data)
{
  (void) editable;
  update_template_preview ((TemplatePreviewCtx *) user_data);
}

static Journal *
run_journal_editor_dialog (GtkWindow *parent, const Journal *existing)
{
  gboolean editing = (existing != NULL);
  GtkWidget *dialog = gtk_dialog_new_with_buttons (
      editing ? "Edit Journal" : "Add Journal", parent, GTK_DIALOG_MODAL,
      "_Cancel", GTK_RESPONSE_CANCEL, editing ? "_Save" : "_Add", GTK_RESPONSE_OK, NULL);
  gtk_window_set_default_size (GTK_WINDOW (dialog), 560, 600);

  GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  GtkWidget *outer_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (outer_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  /* Size the scrolled window to the form's natural height (up to a cap)
   * instead of a small default unrelated to its content — so the dialog
   * simply opens tall enough to show everything on a typical screen,
   * without depending on a user finding/dragging a scrollbar. Real
   * scrolling (GTK_POLICY_AUTOMATIC above) still kicks in as a fallback
   * if the screen is too short for max_content_height. */
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (outer_scroller), TRUE);
  gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (outer_scroller), 300);
  gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (outer_scroller), 720);
  gtk_container_add (GTK_CONTAINER (content), outer_scroller);

  GtkWidget *sections_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_set_border_width (GTK_CONTAINER (sections_box), 12);
  gtk_container_add (GTK_CONTAINER (outer_scroller), sections_box);

  GtkWidget *journal_frame = gtk_frame_new ("Journal");
  GtkWidget *journal_grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (journal_grid), 8);
  gtk_grid_set_column_spacing (GTK_GRID (journal_grid), 8);
  gtk_container_set_border_width (GTK_CONTAINER (journal_grid), 10);
  gtk_container_add (GTK_CONTAINER (journal_frame), journal_grid);
  gtk_box_pack_start (GTK_BOX (sections_box), journal_frame, FALSE, FALSE, 0);

  GtkWidget *name_entry = gtk_entry_new ();
  if (existing)
    gtk_entry_set_text (GTK_ENTRY (name_entry), existing->name);
  gtk_grid_attach (GTK_GRID (journal_grid), gtk_label_new ("Name:"), 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (journal_grid), name_entry, 1, 0, 2, 1);

  GtkWidget *root_entry = gtk_entry_new ();
  if (existing)
    gtk_entry_set_text (GTK_ENTRY (root_entry), existing->root_dir);
  GtkWidget *browse_btn = gtk_button_new_with_label ("Browse…");
  gtk_grid_attach (GTK_GRID (journal_grid), gtk_label_new ("Root directory:"), 0, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (journal_grid), root_entry, 1, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (journal_grid), browse_btn, 2, 1, 1, 1);
  g_signal_connect (browse_btn, "clicked", G_CALLBACK (on_browse_root_dir_clicked), root_entry);

  GtkWidget *layout_entry = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (layout_entry),
                       (existing && existing->layout_template && *existing->layout_template)
                       ? existing->layout_template : LAYOUT_DEFAULT_TEMPLATE);
  gtk_grid_attach (GTK_GRID (journal_grid), gtk_label_new ("Layout template:"), 0, 2, 1, 1);
  gtk_grid_attach (GTK_GRID (journal_grid), layout_entry, 1, 2, 2, 1);

  GtkWidget *layout_hint = gtk_label_new (
      "Path to each day's file, relative to the root directory. Tokens: {YYYY} {MM} {DD}. "
      "Example (flat, no subfolders): \"{YYYY}-{MM}-{DD}.md\"");
  gtk_label_set_line_wrap (GTK_LABEL (layout_hint), TRUE);
  gtk_label_set_line_wrap_mode (GTK_LABEL (layout_hint), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars (GTK_LABEL (layout_hint), 64);
  gtk_label_set_xalign (GTK_LABEL (layout_hint), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (layout_hint), "dim-label");
  gtk_grid_attach (GTK_GRID (journal_grid), layout_hint, 0, 3, 3, 1);

  GtkWidget *author_entry = gtk_entry_new ();
  if (existing && existing->author)
    gtk_entry_set_text (GTK_ENTRY (author_entry), existing->author);
  gtk_grid_attach (GTK_GRID (journal_grid), gtk_label_new ("Author (optional):"), 0, 4, 1, 1);
  gtk_grid_attach (GTK_GRID (journal_grid), author_entry, 1, 4, 2, 1);

  GtkWidget *template_frame = gtk_frame_new ("New-file template");
  GtkWidget *template_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (template_box), 10);
  gtk_container_add (GTK_CONTAINER (template_frame), template_box);
  gtk_box_pack_start (GTK_BOX (sections_box), template_frame, FALSE, FALSE, 0);

  GtkWidget *template_hint = gtk_label_new (
      "Written verbatim to create a day's file the first time it's needed. Tokens: "
      "{DATE} {AUTHOR} {YYYY} {MM} {DD}. Leave blank for a genuinely empty new file. "
      "A category's marker only lands in its \"natural\" spot on a new file if this "
      "template already contains that exact marker line — otherwise the first entry "
      "for it falls back to appending the marker at the end of the file.");
  gtk_label_set_line_wrap (GTK_LABEL (template_hint), TRUE);
  gtk_label_set_line_wrap_mode (GTK_LABEL (template_hint), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars (GTK_LABEL (template_hint), 64);
  gtk_label_set_xalign (GTK_LABEL (template_hint), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (template_hint), "dim-label");
  gtk_box_pack_start (GTK_BOX (template_box), template_hint, FALSE, FALSE, 0);

  GtkWidget *template_view = gtk_text_view_new ();
  GtkTextBuffer *template_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (template_view));
  gtk_text_buffer_set_text (template_buffer,
                             (existing && existing->new_file_template) ? existing->new_file_template : "", -1);
  GtkWidget *template_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (template_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (template_scroller), GTK_SHADOW_IN);
  gtk_widget_set_size_request (template_scroller, -1, 160);
  gtk_container_add (GTK_CONTAINER (template_scroller), template_view);
  gtk_box_pack_start (GTK_BOX (template_box), template_scroller, TRUE, TRUE, 0);

  GtkWidget *preview_label = gtk_label_new ("Preview for today:");
  gtk_label_set_xalign (GTK_LABEL (preview_label), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (preview_label), "dim-label");
  gtk_box_pack_start (GTK_BOX (template_box), preview_label, FALSE, FALSE, 0);

  GtkWidget *preview_view = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (preview_view), FALSE);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (preview_view), FALSE);
  gtk_widget_set_can_focus (preview_view, FALSE);
  GtkTextBuffer *preview_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (preview_view));
  GtkWidget *preview_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (preview_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (preview_scroller), GTK_SHADOW_IN);
  gtk_widget_set_size_request (preview_scroller, -1, 120);
  gtk_container_add (GTK_CONTAINER (preview_scroller), preview_view);
  gtk_box_pack_start (GTK_BOX (template_box), preview_scroller, TRUE, TRUE, 0);

  TemplatePreviewCtx preview_ctx = { template_buffer, author_entry, preview_buffer };
  g_signal_connect (template_buffer, "changed", G_CALLBACK (on_template_preview_buffer_changed), &preview_ctx);
  g_signal_connect (author_entry, "changed", G_CALLBACK (on_template_preview_author_changed), &preview_ctx);
  update_template_preview (&preview_ctx);

  GtkWidget *categories_frame = gtk_frame_new ("Categories");
  GtkWidget *categories_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (categories_box), 10);
  gtk_container_add (GTK_CONTAINER (categories_frame), categories_box);
  gtk_box_pack_start (GTK_BOX (sections_box), categories_frame, FALSE, FALSE, 0);

  CategoriesEditor cat_editor = { NULL, NULL, NULL };
  if (existing)
    for (GList *l = existing->categories; l != NULL; l = l->next)
      cat_editor.categories = g_list_append (cat_editor.categories, category_copy ((Category *) l->data));

  cat_editor.store = gtk_list_store_new (N_C_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  cat_editor.tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (cat_editor.store));
  g_object_unref (cat_editor.store);

  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (cat_editor.tree_view), -1, "Label",
                                                gtk_cell_renderer_text_new (), "text", COL_C_LABEL, NULL);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (cat_editor.tree_view), -1, "Marker",
                                                gtk_cell_renderer_text_new (), "text", COL_C_MARKER, NULL);

  GtkWidget *cat_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (cat_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (cat_scroller, -1, 140);
  gtk_container_add (GTK_CONTAINER (cat_scroller), cat_editor.tree_view);
  gtk_box_pack_start (GTK_BOX (categories_box), cat_scroller, TRUE, TRUE, 0);

  GtkWidget *cat_button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *cat_add_btn = gtk_button_new_with_label ("Add…");
  GtkWidget *cat_edit_btn = gtk_button_new_with_label ("Edit…");
  GtkWidget *cat_remove_btn = gtk_button_new_with_label ("Remove");
  GtkWidget *cat_move_up_btn = gtk_button_new_with_label ("Move up");
  GtkWidget *cat_move_down_btn = gtk_button_new_with_label ("Move down");
  gtk_box_pack_start (GTK_BOX (cat_button_box), cat_add_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (cat_button_box), cat_edit_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (cat_button_box), cat_remove_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (cat_button_box), cat_move_up_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (cat_button_box), cat_move_down_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (categories_box), cat_button_box, FALSE, FALSE, 0);

  g_signal_connect (cat_add_btn, "clicked", G_CALLBACK (on_add_category_clicked), &cat_editor);
  g_signal_connect (cat_edit_btn, "clicked", G_CALLBACK (on_edit_category_clicked), &cat_editor);
  g_signal_connect (cat_remove_btn, "clicked", G_CALLBACK (on_remove_category_clicked), &cat_editor);
  g_signal_connect (cat_move_up_btn, "clicked", G_CALLBACK (on_move_category_up_clicked), &cat_editor);
  g_signal_connect (cat_move_down_btn, "clicked", G_CALLBACK (on_move_category_down_clicked), &cat_editor);

  categories_editor_refresh_store (&cat_editor);

  JournalValidateCtx vctx = { dialog, name_entry, root_entry };
  g_signal_connect (name_entry, "changed", G_CALLBACK (validate_journal_editor), &vctx);
  g_signal_connect (root_entry, "changed", G_CALLBACK (validate_journal_editor), &vctx);
  validate_journal_editor (NULL, &vctx);

  gtk_widget_show_all (dialog);

  Journal *result = NULL;
  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
      result = editing ? journal_copy (existing) : journal_new ();

      g_free (result->name);
      result->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (name_entry)));
      g_free (result->root_dir);
      result->root_dir = g_strdup (gtk_entry_get_text (GTK_ENTRY (root_entry)));

      const gchar *layout_text = gtk_entry_get_text (GTK_ENTRY (layout_entry));
      g_free (result->layout_template);
      result->layout_template = g_strdup ((layout_text && *layout_text) ? layout_text : LAYOUT_DEFAULT_TEMPLATE);

      g_free (result->author);
      result->author = g_strdup (gtk_entry_get_text (GTK_ENTRY (author_entry)));

      GtkTextIter t_start, t_end;
      gtk_text_buffer_get_bounds (template_buffer, &t_start, &t_end);
      g_free (result->new_file_template);
      result->new_file_template = gtk_text_buffer_get_text (template_buffer, &t_start, &t_end, FALSE);

      if (!editing)
        result->id = g_uuid_string_random ();

      g_list_free_full (result->categories, (GDestroyNotify) category_free);
      result->categories = cat_editor.categories; /* ownership transferred */
      cat_editor.categories = NULL; /* prevent the cleanup below from double-freeing */
    }

  if (cat_editor.categories)
    g_list_free_full (cat_editor.categories, (GDestroyNotify) category_free);

  gtk_widget_destroy (dialog);
  return result;
}

static void
on_add_journal_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  PrefsContext *ctx = user_data;
  Journal *new_journal = run_journal_editor_dialog (GTK_WINDOW (gtk_widget_get_toplevel (ctx->tree_view)), NULL);
  if (!new_journal)
    return;

  if (config_find_journal_by_name (ctx->cfg, new_journal->name))
    {
      show_error (ctx, "A journal with that name already exists.");
      journal_free (new_journal);
      return;
    }

  ctx->cfg->journals = g_list_append (ctx->cfg->journals, new_journal);
  save_and_apply (ctx);
}

static void
on_edit_journal_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  PrefsContext *ctx = user_data;
  gchar *selected_id = get_selected_journal_id (ctx);
  if (!selected_id)
    return;

  GList *node = find_journal_node (ctx->cfg, selected_id);
  if (!node)
    {
      g_free (selected_id);
      return;
    }

  Journal *existing = node->data;
  Journal *edited = run_journal_editor_dialog (GTK_WINDOW (gtk_widget_get_toplevel (ctx->tree_view)), existing);
  if (!edited)
    {
      g_free (selected_id);
      return;
    }

  for (GList *l = ctx->cfg->journals; l != NULL; l = l->next)
    {
      Journal *other = l->data;
      if (other != existing && g_strcmp0 (other->name, edited->name) == 0)
        {
          show_error (ctx, "A journal with that name already exists.");
          journal_free (edited);
          g_free (selected_id);
          return;
        }
    }

  journal_free (existing);
  node->data = edited;
  save_and_apply (ctx);
  g_free (selected_id);
}

static void
on_remove_journal_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  PrefsContext *ctx = user_data;
  gchar *selected_id = get_selected_journal_id (ctx);
  if (!selected_id)
    return;

  GList *node = find_journal_node (ctx->cfg, selected_id);
  if (!node)
    {
      g_free (selected_id);
      return;
    }

  Journal *journal = node->data;
  GtkWidget *confirm = gtk_message_dialog_new (
      GTK_WINDOW (gtk_widget_get_toplevel (ctx->tree_view)), GTK_DIALOG_MODAL,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Remove journal \"%s\"?", journal->name);
  gint response = gtk_dialog_run (GTK_DIALOG (confirm));
  gtk_widget_destroy (confirm);
  if (response != GTK_RESPONSE_YES)
    {
      g_free (selected_id);
      return;
    }

  ctx->cfg->journals = g_list_delete_link (ctx->cfg->journals, node);
  journal_free (journal);

  save_and_apply (ctx);
  g_free (selected_id);
}

static void
on_move_journal_up_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  PrefsContext *ctx = user_data;
  gchar *selected_id = get_selected_journal_id (ctx);
  if (!selected_id)
    return;

  GList *node = find_journal_node (ctx->cfg, selected_id);
  if (node && node->prev)
    {
      swap_journal_positions (node, node->prev);
      save_and_apply (ctx);
      select_journal_row (ctx, selected_id);
    }
  g_free (selected_id);
}

static void
on_move_journal_down_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  PrefsContext *ctx = user_data;
  gchar *selected_id = get_selected_journal_id (ctx);
  if (!selected_id)
    return;

  GList *node = find_journal_node (ctx->cfg, selected_id);
  if (node && node->next)
    {
      swap_journal_positions (node, node->next);
      save_and_apply (ctx);
      select_journal_row (ctx, selected_id);
    }
  g_free (selected_id);
}

static void
open_uri_or_show_error (GtkWidget *relative_widget, const gchar *uri, const gchar *what)
{
  GError *error = NULL;
  if (!gtk_show_uri_on_window (GTK_WINDOW (gtk_widget_get_toplevel (relative_widget)), uri,
                                gtk_get_current_event_time (), &error))
    {
      GtkWidget *dialog = gtk_message_dialog_new (
          GTK_WINDOW (gtk_widget_get_toplevel (relative_widget)), GTK_DIALOG_MODAL,
          GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Failed to open %s: %s",
          what, error ? error->message : "unknown error");
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      g_clear_error (&error);
    }
}

static void
on_open_config_file_clicked (GtkButton *button, gpointer user_data)
{
  (void) user_data;
  gchar *path = config_file_path ();
  gchar *uri = g_filename_to_uri (path, NULL, NULL);
  if (uri)
    open_uri_or_show_error (GTK_WIDGET (button), uri, "configuration file");
  g_free (uri);
  g_free (path);
}

static void
on_open_config_dir_clicked (GtkButton *button, gpointer user_data)
{
  (void) user_data;
  gchar *path = config_file_path ();
  gchar *dir = g_path_get_dirname (path);
  gchar *uri = g_filename_to_uri (dir, NULL, NULL);
  if (uri)
    open_uri_or_show_error (GTK_WIDGET (button), uri, "configuration folder");
  g_free (uri);
  g_free (dir);
  g_free (path);
}

static void
build_preferences_contents (GtkDialog *dialog, PanelWidget *panel_widget)
{
  PrefsContext *ctx = g_new0 (PrefsContext, 1);
  ctx->cfg = config_load ();
  ctx->panel_widget = panel_widget;

  GtkWidget *content = gtk_dialog_get_content_area (dialog);
  GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
  gtk_container_add (GTK_CONTAINER (content), vbox);

  GtkWidget *journals_frame = gtk_frame_new ("Journals");
  GtkWidget *journals_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (journals_box), 10);
  gtk_container_add (GTK_CONTAINER (journals_frame), journals_box);
  gtk_box_pack_start (GTK_BOX (vbox), journals_frame, TRUE, TRUE, 0);

  GtkWidget *general_frame = gtk_frame_new ("General");
  GtkWidget *general_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (general_box), 10);
  gtk_container_add (GTK_CONTAINER (general_frame), general_box);
  gtk_box_pack_start (GTK_BOX (vbox), general_frame, FALSE, FALSE, 0);

  ctx->store = gtk_list_store_new (N_J_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  ctx->tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (ctx->store));
  g_object_unref (ctx->store);

  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (ctx->tree_view), -1, "Name",
                                                gtk_cell_renderer_text_new (), "text", COL_J_NAME, NULL);
  {
    GtkCellRenderer *root_renderer = gtk_cell_renderer_text_new ();
    g_object_set (root_renderer, "ellipsize", PANGO_ELLIPSIZE_START, NULL);
    GtkTreeViewColumn *root_col = gtk_tree_view_column_new_with_attributes (
        "Root directory", root_renderer, "text", COL_J_ROOT, NULL);
    gtk_tree_view_column_set_resizable (root_col, TRUE);
    gtk_tree_view_column_set_max_width (root_col, 320);
    gtk_tree_view_insert_column (GTK_TREE_VIEW (ctx->tree_view), root_col, -1);
  }
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (ctx->tree_view), -1, "Categories",
                                                gtk_cell_renderer_text_new (), "text", COL_J_CATEGORY_COUNT, NULL);
  gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (ctx->tree_view), COL_J_ROOT);

  GtkWidget *scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scroller, -1, 200);
  gtk_container_add (GTK_CONTAINER (scroller), ctx->tree_view);
  gtk_box_pack_start (GTK_BOX (journals_box), scroller, TRUE, TRUE, 0);

  GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *add_btn = gtk_button_new_with_label ("Add…");
  GtkWidget *edit_btn = gtk_button_new_with_label ("Edit…");
  GtkWidget *remove_btn = gtk_button_new_with_label ("Remove");
  GtkWidget *move_up_btn = gtk_button_new_with_label ("Move up");
  GtkWidget *move_down_btn = gtk_button_new_with_label ("Move down");
  gtk_box_pack_start (GTK_BOX (button_box), add_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (button_box), edit_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (button_box), remove_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (button_box), move_up_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (button_box), move_down_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (journals_box), button_box, FALSE, FALSE, 0);

  GtkWidget *version_label = gtk_label_new ("Version: " PACKAGE_VERSION);
  gtk_label_set_selectable (GTK_LABEL (version_label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (version_label), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (version_label), "dim-label");
  gtk_box_pack_start (GTK_BOX (general_box), version_label, FALSE, FALSE, 0);

  gchar *config_path = config_file_path ();
  gchar *config_path_text = g_strdup_printf ("Config file: %s", config_path);
  GtkWidget *config_path_label = gtk_label_new (config_path_text);
  gtk_label_set_selectable (GTK_LABEL (config_path_label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (config_path_label), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (config_path_label), "dim-label");
  g_free (config_path_text);
  g_free (config_path);

  GtkWidget *config_path_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (config_path_box), config_path_label, TRUE, TRUE, 0);
  GtkWidget *open_file_btn = gtk_button_new_with_label ("Open file");
  GtkWidget *open_dir_btn = gtk_button_new_with_label ("Open folder");
  gtk_box_pack_start (GTK_BOX (config_path_box), open_file_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (config_path_box), open_dir_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (general_box), config_path_box, FALSE, FALSE, 0);

  g_signal_connect (open_file_btn, "clicked", G_CALLBACK (on_open_config_file_clicked), NULL);
  g_signal_connect (open_dir_btn, "clicked", G_CALLBACK (on_open_config_dir_clicked), NULL);

  g_signal_connect (add_btn, "clicked", G_CALLBACK (on_add_journal_clicked), ctx);
  g_signal_connect (edit_btn, "clicked", G_CALLBACK (on_edit_journal_clicked), ctx);
  g_signal_connect (remove_btn, "clicked", G_CALLBACK (on_remove_journal_clicked), ctx);
  g_signal_connect (move_up_btn, "clicked", G_CALLBACK (on_move_journal_up_clicked), ctx);
  g_signal_connect (move_down_btn, "clicked", G_CALLBACK (on_move_journal_down_clicked), ctx);

  refresh_journal_store (ctx);

  g_object_set_data_full (G_OBJECT (dialog), "prefs-context", ctx, prefs_context_free);
}

static void
on_dialog_response (GtkDialog *dialog, gint response_id, gpointer user_data)
{
  (void) response_id;
  XfcePanelPlugin *plugin = user_data;
  xfce_panel_plugin_unblock_menu (plugin);
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

void
preferences_dialog_open (XfcePanelPlugin *plugin, PanelWidget *panel_widget)
{
  GtkWidget *dialog = gtk_dialog_new_with_buttons (
      "Journal Preferences", NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Close", GTK_RESPONSE_CLOSE, NULL);
  gtk_window_set_default_size (GTK_WINDOW (dialog), 620, 480);
  gtk_window_set_screen (GTK_WINDOW (dialog), gtk_widget_get_screen (GTK_WIDGET (plugin)));

  build_preferences_contents (GTK_DIALOG (dialog), panel_widget);

  xfce_panel_plugin_block_menu (plugin);
  g_signal_connect (dialog, "response", G_CALLBACK (on_dialog_response), plugin);
  xfce_panel_plugin_take_window (plugin, GTK_WINDOW (dialog));

  gtk_widget_show_all (dialog);
}
