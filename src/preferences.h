#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include "panel_widget.h"

/* Opens the preferences dialog for this plugin instance (via
 * xfce_panel_plugin_take_window, so the panel manages single-instance
 * semantics). Every mutation inside the dialog is saved to disk immediately
 * and applied to `panel_widget` via panel_widget_reload_config(). */
void preferences_dialog_open (XfcePanelPlugin *plugin, PanelWidget *panel_widget);

#endif
