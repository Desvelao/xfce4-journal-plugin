#include <libxfce4panel/libxfce4panel.h>

#include "panel_widget.h"
#include "preferences.h"

static void journal_widget_free (XfcePanelPlugin *plugin, gpointer user_data);
static void journal_widget_configure (XfcePanelPlugin *plugin, gpointer user_data);
static void journal_widget_about (XfcePanelPlugin *plugin, gpointer user_data);
static void journal_widget_orientation_changed (XfcePanelPlugin *plugin, GtkOrientation orientation, gpointer user_data);

static void
journal_widget_construct (XfcePanelPlugin *plugin)
{
  PanelWidget *widget = panel_widget_new (plugin);

  gtk_container_add (GTK_CONTAINER (plugin), panel_widget_get_button (widget));
  xfce_panel_plugin_add_action_widget (plugin, panel_widget_get_button (widget));
  gtk_widget_show (GTK_WIDGET (plugin));

  g_signal_connect (plugin, "free-data", G_CALLBACK (journal_widget_free), widget);
  g_signal_connect (plugin, "configure-plugin", G_CALLBACK (journal_widget_configure), widget);
  g_signal_connect (plugin, "about", G_CALLBACK (journal_widget_about), widget);
  g_signal_connect (plugin, "orientation-changed", G_CALLBACK (journal_widget_orientation_changed), widget);

  xfce_panel_plugin_menu_show_configure (plugin);
  xfce_panel_plugin_menu_show_about (plugin);
}

static void
journal_widget_free (XfcePanelPlugin *plugin, gpointer user_data)
{
  (void) plugin;
  PanelWidget *widget = user_data;
  panel_widget_free (widget);
}

static void
journal_widget_configure (XfcePanelPlugin *plugin, gpointer user_data)
{
  PanelWidget *widget = user_data;
  preferences_dialog_open (plugin, widget);
}

static void
journal_widget_about (XfcePanelPlugin *plugin, gpointer user_data)
{
  (void) plugin;
  (void) user_data;
  GtkWidget *dialog = gtk_message_dialog_new (
      NULL, 0, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
      "Journal %s\n\nAdd dated entries to one or more markdown journals from the panel — each "
      "journal defines its own day-file layout, new-file template, and categories, and every "
      "entry is spliced into the right section of today's file.",
      PACKAGE_VERSION);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
journal_widget_orientation_changed (XfcePanelPlugin *plugin, GtkOrientation orientation, gpointer user_data)
{
  (void) plugin;
  PanelWidget *widget = user_data;
  panel_widget_set_orientation (widget, orientation);
}

XFCE_PANEL_PLUGIN_REGISTER (journal_widget_construct);
