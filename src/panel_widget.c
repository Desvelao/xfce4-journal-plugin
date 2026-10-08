#include "panel_widget.h"
#include "config.h"
#include "layout.h"
#include "entry_writer.h"
#include "preferences.h"

#include <glib/gstdio.h>

struct _PanelWidget {
  XfcePanelPlugin *plugin;
  GtkWidget *button;
  GtkWidget *icon;
  GtkWidget *menu;

  Config *cfg;
  gchar *active_journal_id; /* nullable; which journal's categories the "Add
                                entry" section of the flat menu currently
                                shows — see get_active_journal(). Set via a
                                journal's "Set as active" submenu row, not by
                                clicking the journal row itself (that row has
                                its own submenu, so GTK never fires its
                                "activate"). Session-only, never persisted. */
};

static void rebuild_menu (PanelWidget *self);
static Journal *get_active_journal (PanelWidget *self);
static GtkWidget *make_menu_section_header (const gchar *text);
static GtkWidget *build_journal_actions_submenu (PanelWidget *self, Journal *journal, gboolean is_active);
static GtkWidget *make_journal_menu_item (PanelWidget *self, Journal *journal, gboolean is_active);
static void append_journal_body_items (PanelWidget *self, Journal *journal);
static gboolean on_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void on_journal_set_active_activate (GtkMenuItem *item, gpointer user_data);
static void on_category_menu_item_activate (GtkMenuItem *item, gpointer user_data);
static void on_open_today_activate (GtkMenuItem *item, gpointer user_data);
static void on_open_folder_activate (GtkMenuItem *item, gpointer user_data);
static void on_preferences_menu_item_activate (GtkMenuItem *item, gpointer user_data);
static void run_add_entry_dialog (PanelWidget *self, Journal *journal, Category *category);
static void open_uri_or_show_error (GtkWidget *relative_widget, const gchar *uri, const gchar *what);

PanelWidget *
panel_widget_new (XfcePanelPlugin *plugin)
{
  PanelWidget *self = g_new0 (PanelWidget, 1);
  self->plugin = plugin;
  self->cfg = config_load ();

  self->button = xfce_panel_create_button ();
  self->icon = gtk_image_new_from_icon_name ("xfce4-journal-plugin-symbolic", GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (self->button), self->icon);
  gtk_widget_show_all (self->button);
  gtk_widget_set_tooltip_text (self->button, "Journal");

  self->menu = gtk_menu_new ();
  gtk_menu_attach_to_widget (GTK_MENU (self->menu), self->button, NULL);

  g_signal_connect (self->button, "button-press-event", G_CALLBACK (on_button_press), self);

  rebuild_menu (self);

  return self;
}

GtkWidget *
panel_widget_get_button (PanelWidget *self)
{
  return self->button;
}

void
panel_widget_free (PanelWidget *self)
{
  if (!self)
    return;
  config_free (self->cfg);
  g_free (self->active_journal_id);
  g_free (self);
}

void
panel_widget_set_orientation (PanelWidget *self, GtkOrientation orientation)
{
  (void) self;
  (void) orientation;
  /* Icon-only button with no live-updating label — nothing here depends
   * on panel orientation. */
}

void
panel_widget_reload_config (PanelWidget *self)
{
  config_free (self->cfg);
  self->cfg = config_load ();
  rebuild_menu (self);
}

static gboolean
on_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  (void) widget;
  PanelWidget *self = user_data;

  if (event->button != 1)
    return FALSE; /* let right-click propagate to the panel's own context menu */

  rebuild_menu (self);
  gtk_menu_popup_at_pointer (GTK_MENU (self->menu), (GdkEvent *) event);
  return TRUE;
}

/* A non-interactive, bold-labeled row acting as a section title within a
 * flat GtkMenu — plain GtkMenu has no formal "section" widget, so this is
 * the standard idiom. */
static GtkWidget *
make_menu_section_header (const gchar *text)
{
  GtkWidget *item = gtk_menu_item_new ();
  gtk_widget_set_sensitive (item, FALSE);
  GtkWidget *label = gtk_label_new (NULL);
  gchar *markup = g_markup_printf_escaped ("<b>%s</b>", text);
  gtk_label_set_markup (GTK_LABEL (label), markup);
  g_free (markup);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_container_add (GTK_CONTAINER (item), label);
  gtk_widget_show_all (item);
  return item;
}

/* The per-journal submenu attached to its "Journals" row: open its files,
 * or make it the active journal (governing which journal's categories the
 * flat "Add entry" section below shows). A GtkMenuItem with a submenu never
 * fires its own "activate" on click — GTK just opens the submenu — so
 * making a journal active has to live in here rather than on the row
 * itself; the "Set as active" row is disabled when it would be a no-op. */
static GtkWidget *
build_journal_actions_submenu (PanelWidget *self, Journal *journal, gboolean is_active)
{
  GtkWidget *submenu = gtk_menu_new ();

  GtkWidget *open_today_item = gtk_menu_item_new_with_label ("Open today's entry");
  g_object_set_data (G_OBJECT (open_today_item), "journal-id", journal->id);
  g_signal_connect (open_today_item, "activate", G_CALLBACK (on_open_today_activate), self);
  gtk_widget_show (open_today_item);
  gtk_menu_shell_append (GTK_MENU_SHELL (submenu), open_today_item);

  GtkWidget *open_folder_item = gtk_menu_item_new_with_label ("Open journal folder");
  g_object_set_data (G_OBJECT (open_folder_item), "journal-id", journal->id);
  g_signal_connect (open_folder_item, "activate", G_CALLBACK (on_open_folder_activate), self);
  gtk_widget_show (open_folder_item);
  gtk_menu_shell_append (GTK_MENU_SHELL (submenu), open_folder_item);

  GtkWidget *sep = gtk_separator_menu_item_new ();
  gtk_widget_show (sep);
  gtk_menu_shell_append (GTK_MENU_SHELL (submenu), sep);

  GtkWidget *set_active_item = gtk_menu_item_new_with_label ("Set as active");
  g_object_set_data (G_OBJECT (set_active_item), "journal-id", journal->id);
  gtk_widget_set_sensitive (set_active_item, !is_active);
  g_signal_connect (set_active_item, "activate", G_CALLBACK (on_journal_set_active_activate), self);
  gtk_widget_show (set_active_item);
  gtk_menu_shell_append (GTK_MENU_SHELL (submenu), set_active_item);

  return submenu;
}

/* One row of the "Journals" selector: a plain GtkMenuItem, bold-labeled and
 * marked with an object-select-symbolic icon when it's the active journal,
 * rather than using a GtkCheckMenuItem. Its
 * submenu (see build_journal_actions_submenu()) carries this journal's
 * actions and the means to make it active. */
static GtkWidget *
make_journal_menu_item (PanelWidget *self, Journal *journal, gboolean is_active)
{
  GtkWidget *item = gtk_menu_item_new ();
  g_object_set_data (G_OBJECT (item), "journal-id", journal->id);

  GtkWidget *row = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (row), 6);
  gtk_container_add (GTK_CONTAINER (item), row);

  gint col = 0;

  if (is_active)
    {
      GtkWidget *marker = gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU);
      gtk_grid_attach (GTK_GRID (row), marker, col++, 0, 1, 1);
    }

  GtkWidget *label = gtk_label_new (NULL);
  if (is_active)
    {
      gchar *markup = g_markup_printf_escaped ("<b>%s</b>", journal->name);
      gtk_label_set_markup (GTK_LABEL (label), markup);
      g_free (markup);
    }
  else
    {
      gtk_label_set_text (GTK_LABEL (label), journal->name);
    }
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_grid_attach (GTK_GRID (row), label, col++, 0, 1, 1);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), build_journal_actions_submenu (self, journal, is_active));

  gtk_widget_show_all (item);
  return item;
}

/* The journal whose categories the flat "Add entry" section currently shows: the
 * one matching active_journal_id if it still exists, else the first
 * configured journal, else NULL. Session-only state — a journal renamed or
 * removed in Preferences just falls back silently, no explicit reset
 * needed elsewhere. */
static Journal *
get_active_journal (PanelWidget *self)
{
  Journal *journal = config_find_journal (self->cfg, self->active_journal_id);
  if (journal)
    return journal;
  return self->cfg->journals ? (Journal *) self->cfg->journals->data : NULL;
}

static void
append_journal_body_items (PanelWidget *self, Journal *journal)
{
  gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), make_menu_section_header ("Add entry"));

  if (journal->categories == NULL)
    {
      GtkWidget *item = gtk_menu_item_new_with_label ("No categories configured for this journal.");
      gtk_widget_set_sensitive (item, FALSE);
      gtk_widget_show (item);
      gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), item);
      return;
    }

  for (GList *l = journal->categories; l != NULL; l = l->next)
    {
      Category *category = l->data;
      const gchar *label = (category->label && *category->label) ? category->label : category->marker;
      GtkWidget *item = gtk_menu_item_new_with_label (label);
      g_object_set_data (G_OBJECT (item), "journal-id", journal->id);
      g_object_set_data (G_OBJECT (item), "category-id", category->id);
      g_signal_connect (item, "activate", G_CALLBACK (on_category_menu_item_activate), self);
      gtk_widget_show (item);
      gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), item);
    }
}

static void
rebuild_menu (PanelWidget *self)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (self->menu));
  for (GList *l = children; l != NULL; l = l->next)
    gtk_widget_destroy (GTK_WIDGET (l->data));
  g_list_free (children);

  if (self->cfg->journals == NULL)
    {
      GtkWidget *item = gtk_menu_item_new_with_label ("No journals configured — open Preferences to add one.");
      gtk_widget_set_sensitive (item, FALSE);
      gtk_widget_show (item);
      gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), item);
    }
  else
    {
      Journal *active = get_active_journal (self);

      /* A flat "Journals" selector — one plain GtkMenuItem per journal,
       * bold-labeled and icon-marked when active (see
       * make_journal_menu_item()) — always shown when at least one journal
       * is configured. No widget state to keep in sync: every item is
       * destroyed and rebuilt fresh from active_journal_id each time the
       * menu is opened. Each row carries its own submenu (open today's
       * entry/folder, plus a "Set as active" row) rather than reacting to a
       * click on the row itself — see build_journal_actions_submenu(). */
      gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), make_menu_section_header ("Journals"));

      for (GList *l = self->cfg->journals; l != NULL; l = l->next)
        {
          Journal *journal = l->data;
          GtkWidget *item = make_journal_menu_item (self, journal, journal == active);
          gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), item);
        }

      GtkWidget *journal_sep = gtk_separator_menu_item_new ();
      gtk_widget_show (journal_sep);
      gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), journal_sep);

      if (active)
        append_journal_body_items (self, active);
    }

  GtkWidget *sep = gtk_separator_menu_item_new ();
  gtk_widget_show (sep);
  gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), sep);

  gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), make_menu_section_header ("General"));

  GtkWidget *prefs_item = gtk_menu_item_new_with_label ("Preferences…");
  g_signal_connect (prefs_item, "activate", G_CALLBACK (on_preferences_menu_item_activate), self);
  gtk_widget_show (prefs_item);
  gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), prefs_item);
}

static void
on_journal_set_active_activate (GtkMenuItem *item, gpointer user_data)
{
  PanelWidget *self = user_data;
  const gchar *journal_id = g_object_get_data (G_OBJECT (item), "journal-id");
  if (!journal_id || g_strcmp0 (self->active_journal_id, journal_id) == 0)
    return;

  g_free (self->active_journal_id);
  self->active_journal_id = g_strdup (journal_id);
}

static void
on_category_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
  PanelWidget *self = user_data;
  const gchar *journal_id = g_object_get_data (G_OBJECT (item), "journal-id");
  const gchar *category_id = g_object_get_data (G_OBJECT (item), "category-id");
  Journal *journal = config_find_journal (self->cfg, journal_id);
  Category *category = journal ? journal_find_category (journal, category_id) : NULL;
  if (journal && category)
    run_add_entry_dialog (self, journal, category);
}

static void
run_add_entry_dialog (PanelWidget *self, Journal *journal, Category *category)
{
  const gchar *category_label = (category->label && *category->label) ? category->label : category->marker;
  gchar *title = g_strdup_printf ("Add to “%s” — %s", category_label, journal->name);

  GtkWidget *dialog = gtk_dialog_new_with_buttons (
      title, GTK_WINDOW (gtk_widget_get_toplevel (self->button)), GTK_DIALOG_MODAL,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_OK, NULL);
  g_free (title);

  GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

  GtkWidget *view = gtk_text_view_new ();
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD_CHAR);

  GtkWidget *scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroller), GTK_SHADOW_IN);
  gtk_widget_set_size_request (scroller, 420, 160);
  gtk_container_set_border_width (GTK_CONTAINER (scroller), 8);
  gtk_container_add (GTK_CONTAINER (scroller), view);
  gtk_container_add (GTK_CONTAINER (content), scroller);

  GtkWidget *hint = gtk_label_new (category->entry_type == ENTRY_TYPE_TODO ? "Each non-empty line becomes its own to-do item under this category."
      : category->entry_type == ENTRY_TYPE_CUSTOM ? "Each non-empty line is added using this category's custom entry format."
      : "Each non-empty line becomes its own bullet under this category.");
  gtk_label_set_xalign (GTK_LABEL (hint), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (hint), "dim-label");
  gtk_container_set_border_width (GTK_CONTAINER (hint), 4);
  gtk_container_add (GTK_CONTAINER (content), hint);

  gtk_widget_show_all (dialog);
  gtk_widget_grab_focus (view);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
      GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
      GtkTextIter start, end;
      gtk_text_buffer_get_bounds (buffer, &start, &end);
      gchar *text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
      gchar **entry_lines = g_strsplit (text, "\n", -1);
      g_free (text);

      GDateTime *today = g_date_time_new_now_local ();
      GError *error = NULL;
      if (!entry_writer_add_entries (journal, today, category->marker, category->marker_occurrence,
                                  category->entry_type, category->entry_template, entry_lines, &error))
        {
          GtkWidget *error_dialog = gtk_message_dialog_new (
              GTK_WINDOW (gtk_widget_get_toplevel (self->button)), GTK_DIALOG_MODAL,
              GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Failed to add entry to \"%s\": %s",
              journal->name, error ? error->message : "unknown error");
          gtk_dialog_run (GTK_DIALOG (error_dialog));
          gtk_widget_destroy (error_dialog);
          g_clear_error (&error);
        }
      g_date_time_unref (today);
      g_strfreev (entry_lines);
    }

  gtk_widget_destroy (dialog);
}

static void
on_open_today_activate (GtkMenuItem *item, gpointer user_data)
{
  PanelWidget *self = user_data;
  const gchar *journal_id = g_object_get_data (G_OBJECT (item), "journal-id");
  Journal *journal = config_find_journal (self->cfg, journal_id);
  if (!journal)
    return;

  GDateTime *today = g_date_time_new_now_local ();
  GError *error = NULL;
  if (entry_writer_ensure_file_exists (journal, today, &error))
    {
      gchar *path = layout_today_absolute_path (journal);
      gchar *uri = g_filename_to_uri (path, NULL, NULL);
      if (uri)
        open_uri_or_show_error (self->button, uri, "today's entry");
      g_free (uri);
      g_free (path);
    }
  else
    {
      GtkWidget *dialog = gtk_message_dialog_new (
          GTK_WINDOW (gtk_widget_get_toplevel (self->button)), GTK_DIALOG_MODAL,
          GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Failed to open today's entry for \"%s\": %s",
          journal->name, error ? error->message : "unknown error");
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      g_clear_error (&error);
    }
  g_date_time_unref (today);
}

static void
on_open_folder_activate (GtkMenuItem *item, gpointer user_data)
{
  PanelWidget *self = user_data;
  const gchar *journal_id = g_object_get_data (G_OBJECT (item), "journal-id");
  Journal *journal = config_find_journal (self->cfg, journal_id);
  if (!journal)
    return;

  /* Defensive: a brand-new journal with zero entries yet would otherwise
   * have no folder to open. */
  g_mkdir_with_parents (journal->root_dir, 0755);

  gchar *uri = g_filename_to_uri (journal->root_dir, NULL, NULL);
  if (uri)
    open_uri_or_show_error (self->button, uri, "journal folder");
  g_free (uri);
}

static void
on_preferences_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
  (void) item;
  PanelWidget *self = user_data;
  preferences_dialog_open (self->plugin, self);
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
