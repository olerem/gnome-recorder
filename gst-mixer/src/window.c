/* GNOME Volume Control
 * Copyright (C) 2003-2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * window.c: main window
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <gnome.h>

#include "keys.h"
#include "preferences.h"
#include "window.h"

static void	gnome_volume_control_window_class_init	(GnomeVolumeControlWindowClass *klass);
static void	gnome_volume_control_window_init	(GnomeVolumeControlWindow *win);
static void	gnome_volume_control_window_dispose	(GObject *object);

static void	cb_change			(GtkWidget       *widget,
						 gpointer         data);
static void	cb_exit				(GtkWidget       *widget,
						 gpointer         data);
static void	cb_preferences			(GtkWidget       *widget,
						 gpointer         data);
static void	cb_about			(GtkWidget       *widget,
						 gpointer         data);

static void	cb_gconf			(GConfClient     *client,
						 guint            connection_id,
						 GConfEntry      *entry,
						 gpointer         data);

#if 0
static void	cb_error			(GstElement      *element,
						 GstElement      *source,
						 GError          *error,
						 gchar           *debug,
						 gpointer         data);
#endif

static GnomeAppClass *parent_class = NULL;

GType
gnome_volume_control_window_get_type (void)
{
  static GType gnome_volume_control_window_type = 0;

  if (!gnome_volume_control_window_type) {
    static const GTypeInfo gnome_volume_control_window_info = {
      sizeof (GnomeVolumeControlWindowClass),
      NULL,
      NULL,
      (GClassInitFunc) gnome_volume_control_window_class_init,
      NULL,
      NULL,
      sizeof (GnomeVolumeControlWindow),
      0,
      (GInstanceInitFunc) gnome_volume_control_window_init,
      NULL
    };

    gnome_volume_control_window_type =
	g_type_register_static (GNOME_TYPE_APP, 
				"GnomeVolumeControlWindow",
				&gnome_volume_control_window_info, 0);
  }

  return gnome_volume_control_window_type;
}

static void
gnome_volume_control_window_class_init (GnomeVolumeControlWindowClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_ref (GNOME_TYPE_APP);

  gobject_class->dispose = gnome_volume_control_window_dispose;
}

/*
 * Menus.
 */

static GnomeUIInfo radio_menu[] = {
  GNOMEUIINFO_RADIOLIST (NULL),
  GNOMEUIINFO_END
};

static GnomeUIInfo file_menu[] = {
  GNOMEUIINFO_SUBTREE_HINT (N_("Change device"),
			    N_("Control volume on a different device"),
			    radio_menu),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_MENU_EXIT_ITEM (cb_exit, NULL),
  GNOMEUIINFO_END
};

static GnomeUIInfo edit_menu[] = {
  GNOMEUIINFO_MENU_PREFERENCES_ITEM (cb_preferences, NULL),
  GNOMEUIINFO_END
};

static GnomeUIInfo help_menu[] = {
  GNOMEUIINFO_HELP ("gnome-volume-control"),
  GNOMEUIINFO_MENU_ABOUT_ITEM (cb_about, NULL),
  GNOMEUIINFO_END
};

static GnomeUIInfo menu[] = {
  GNOMEUIINFO_MENU_FILE_TREE (file_menu),
  GNOMEUIINFO_MENU_EDIT_TREE (edit_menu),
  GNOMEUIINFO_MENU_HELP_TREE (help_menu),
  GNOMEUIINFO_END
};

static void
gnome_volume_control_window_init (GnomeVolumeControlWindow *win)
{
  win->elements = NULL;
  win->element_menu = NULL;
  win->el = NULL;
  win->client = gconf_client_get_default ();
  win->prefs = NULL;

  /* init */
  gnome_app_construct (GNOME_APP (win),
		       "gnome-volume-control", _("Volume Control"));

  /* reasonable (...) default size */
  gtk_widget_set_size_request (GTK_WIDGET (win), 500, -1);
}

GtkWidget *
gnome_volume_control_window_new (GList *elements)
{
  GConfValue *value;
  const gchar *active_el_str = NULL, *cur_el_str, *cur_de_str;
  GstElement *active_element;
  GList *item;
  GnomeVolumeControlWindow *win;
  GtkWidget *el, *bar;
  GnomeApp *app;
  GnomeUIInfo templ = GNOMEUIINFO_RADIOITEM (NULL, NULL, cb_change, NULL);
  gint count = 0, i;
  gchar *title;

  /* default element to first */
  g_return_val_if_fail (elements != NULL, NULL);
  active_element = elements->data;

  /* window */
  win = g_object_new (GNOME_VOLUME_CONTROL_TYPE_WINDOW, NULL);
  app = GNOME_APP (win);
  win->elements = elements;

  /* menus, and the available elements in a submenu */
  win->element_menu = g_new (GnomeUIInfo, g_list_length (elements) + 1);
  for (count = 0, item = elements; item != NULL; item = item->next, count++) {
    cur_el_str = g_object_get_data (item->data, "gnome-volume-control-name");
    cur_de_str = g_strdup_printf (_("Change device to %s"), cur_el_str);
    win->element_menu[count] = templ;
    win->element_menu[count].label = cur_el_str;
    win->element_menu[count].hint = cur_de_str;
  }
  memset (&win->element_menu[count], 0, sizeof (GnomeUIInfo));
  win->element_menu[count].type = GNOME_APP_UI_ENDOFINFO;
  radio_menu[0].moreinfo = win->element_menu;
  gnome_app_create_menus_with_data (app, menu, win);

  /* statusbar */
  bar = gnome_appbar_new (FALSE, TRUE, GNOME_PREFERENCES_USER);
  gnome_app_set_statusbar (app, bar);
  gnome_app_install_appbar_menu_hints (GNOME_APPBAR (bar), menu);

  /* gconf */
  gconf_client_add_dir (win->client, GNOME_VOLUME_CONTROL_KEY_DIR,
			GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);
  gconf_client_notify_add (win->client, GNOME_VOLUME_CONTROL_KEY_DIR,
			   cb_gconf, win, NULL, NULL);

  /* get active element, if any (otherwise we use the default) */
  if ((value = gconf_client_get (win->client,
				 GNOME_VOLUME_CONTROL_KEY_ACTIVE_ELEMENT,
				 NULL)) != NULL &&
      value->type == GCONF_VALUE_STRING) {
    active_el_str = gconf_value_get_string (value);

    for (count = 0, item = elements; item != NULL;
	 item = item->next, count++) {
      cur_el_str = g_object_get_data (item->data, "gnome-volume-control-name");
      if (!strcmp (active_el_str, cur_el_str)) {
        active_element = item->data;
        break;
      }
    }
    if (!item)
      count = 0;
  } else {
    count = 0;
  }

  /* window title and menu selection */
  title = g_strdup_printf (_("Volume Control: %s"),
			   g_object_get_data (G_OBJECT (active_element),
					      "gnome-volume-control-name"));
  gtk_window_set_title (GTK_WINDOW (win), title);
  g_free (title);
  if (count) {
    GTK_CHECK_MENU_ITEM (win->element_menu[0].widget)->active = FALSE;
    GTK_CHECK_MENU_ITEM (win->element_menu[count].widget)->active = TRUE;
  }

  /* add content for this element */
  gst_element_set_state (active_element, GST_STATE_READY);
  el = gnome_volume_control_element_new (active_element,
					 win->client,
					 GNOME_APPBAR (GNOME_APP (win)->statusbar));
  win->el = GNOME_VOLUME_CONTROL_ELEMENT (el);
  gtk_container_set_border_width (GTK_CONTAINER (el), 6);
  gnome_app_set_contents (GNOME_APP (win), el);
  gtk_widget_show (el);

  /* FIXME:
   * - set error handler (cb_error) after device activation:
   *     g_signal_connect (element, "error", G_CALLBACK (cb_error), win);.
   * - on device change: reset error handler, change menu (in case this
   *     was done outside the UI).
   */

  return GTK_WIDGET (win);
}

static void
gnome_volume_control_window_dispose (GObject *object)
{
  GnomeVolumeControlWindow *win = GNOME_VOLUME_CONTROL_WINDOW (object);

  if (win->prefs) {
    gtk_widget_destroy (win->prefs);
    win->prefs = NULL;
  }

  /* clean up */
  if (win->elements) {
    const GList *item;

    for (item = win->elements; item != NULL; item = item->next) {
      GstElement *element = GST_ELEMENT (item->data);

      gst_element_set_state (element, GST_STATE_NULL);
      g_free (g_object_get_data (G_OBJECT (element),
				 "gnome-volume-control-name"));
      gst_object_unref (GST_OBJECT (element));
    }
    g_list_free (win->elements);
    win->elements = NULL;
  }

  if (win->client) {
    g_object_unref (win->client);
    win->client = NULL;
  }

  if (win->element_menu) {
    gint i;

    for (i = 0; win->element_menu[i].widget != NULL; i++) {
      g_free ((void *) win->element_menu[i].hint);
    }
    g_free (win->element_menu);
    win->element_menu = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/*
 * Menu actions.
 */

static void
cb_change (GtkWidget *widget,
	   gpointer   data)
{
  GnomeVolumeControlWindow *win = GNOME_VOLUME_CONTROL_WINDOW (data);
  gint i;

  if (!GTK_CHECK_MENU_ITEM (widget)->active)
    return;

  for (i = 0; win->element_menu[i].widget != NULL; i++) {
    if (win->element_menu[i].widget == widget) {
      GConfValue *value;

      value = gconf_value_new (GCONF_VALUE_STRING);
      gconf_value_set_string (value, win->element_menu[i].label);
      gconf_client_set (win->client,
			GNOME_VOLUME_CONTROL_KEY_ACTIVE_ELEMENT,
			value, NULL);
      gconf_value_free (value);

      break;
    }
  }
}

static void
cb_exit (GtkWidget *widget,
	 gpointer   data)
{
  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
cb_preferences_destroy (GtkWidget *widget,
			gpointer   data)
{
  ((GnomeVolumeControlWindow *) data)->prefs = NULL;
}

static void
cb_preferences (GtkWidget *widget,
		gpointer   data)
{
  GnomeVolumeControlWindow *win = GNOME_VOLUME_CONTROL_WINDOW (data);

  if (!win->prefs) {
    win->prefs = gnome_volume_control_preferences_new (GST_ELEMENT (win->el->mixer),
						       win->client);
    g_signal_connect (win->prefs, "destroy",
		      G_CALLBACK (cb_preferences_destroy), win);
    gtk_widget_show (win->prefs);
  } else {
    gtk_window_present (GTK_WINDOW (win->prefs));
  }
}

static void
cb_about (GtkWidget *widget,
	  gpointer   data)
{
  GtkWidget *about;
  const gchar *authors[] = { "Ronald Bultje <rbultje@ronald.bitfreak.net>",
			     "Leif Johnson <leif@ambient.2y.net>",
			     NULL };
  const gchar *documentors[] = { "Sun Microsystems",
				 NULL};

  about = gnome_about_new (_("Volume Control"),
			   VERSION,
			   "(c) 2003-2004 Ronald Bultje",
			   _("A GNOME/GStreamer-based volume control application"),
			   authors, documentors, NULL,
			   NULL);

  gtk_widget_show (about);
}

/*
 * GConf callback.
 */

static void
cb_gconf (GConfClient *client,
	  guint        connection_id,
	  GConfEntry  *entry,
	  gpointer     data)
{
  GnomeVolumeControlWindow *win = GNOME_VOLUME_CONTROL_WINDOW (data);
  GConfValue *value;
  const gchar *el, *cur_el_str;

  if (!strcmp (gconf_entry_get_key (entry),
	       GNOME_VOLUME_CONTROL_KEY_ACTIVE_ELEMENT) &&
      (value = gconf_entry_get_value (entry)) != NULL &&
      (value->type == GCONF_VALUE_STRING) &&
      (el = gconf_value_get_string (value)) != NULL) {
    GList *item;

    for (item = win->elements; item != NULL; item = item->next) {
      cur_el_str = g_object_get_data (item->data, "gnome-volume-control-name");
      if (!strcmp (cur_el_str, el)) {
        GstElement *old_element = GST_ELEMENT (win->el->mixer);
        gchar *title;

        /* change element */
        gst_element_set_state (item->data, GST_STATE_READY);
        gnome_volume_control_element_change (win->el, item->data);
        if (win->prefs)
          gnome_volume_control_preferences_change (
		GNOME_VOLUME_CONTROL_PREFERENCES (win->prefs), item->data);
        gst_element_set_state (old_element, GST_STATE_NULL);

        /* change window title */
        title = g_strdup_printf (_("Volume Control: %s"), cur_el_str);
        gtk_window_set_title (GTK_WINDOW (win), title);
        g_free (title);

        break;
      }
    }
  }
}

/*
 * Signal handlers.
 */

#if 0
static void
cb_error (GstElement *element,
	  GstElement *source,
	  GError     *error,
	  gchar      *debug,
	  gpointer    data)
{
  GnomeVolumeControlWindow *win = GNOME_VOLUME_CONTROL_WINDOW (data);
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (GTK_WINDOW (win),
                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                   error->message);
  gtk_widget_show (dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}
#endif