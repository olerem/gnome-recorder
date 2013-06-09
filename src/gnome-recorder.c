/*
 *  Authors: Iain Holmes <iain@prettypeople.org>
 *
 * Copyright 2002, 2003, 2004, 2005 Iain Holmes
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
 *
 * 4th Februrary 2005: Christian Schaller: changed license to LGPL with 
 * permission of Iain Holmes, Ronald Bultje, Leontine Binchy (SUN), Johan Dahlin *  and Joe Marcus Clarke
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n.h>
#include <gst/gst.h>
#include <stdlib.h>

#include "gsr-window.h"

void gsr_quit (void);
GtkWidget * gsr_open_window (const char *filename);

static GList *windows = NULL;

static gboolean
delete_event_cb (GSRWindow *window,
		 gpointer data)
{
  return FALSE;
}

static void
window_destroyed (GtkWidget *window,
		  gpointer data)
{
  windows = g_list_remove (windows, window);

  if (windows == NULL) {
    gtk_main_quit ();
  }
}

void
gsr_quit (void) 
{
  GList *p;

  for (p = windows; p;) {
    GSRWindow *window = p->data;

    /* p is set here instead of in the for statement,
       because by the time we get back to the loop,
       p will be invalid */
    p = p->next;

    gsr_window_close (window);
  }
}

GtkWidget *
gsr_open_window (const char *filename)
{
  GtkWidget *window;
  gchar *name = "Gnome Sound Recorder";

  window = GTK_WIDGET (gsr_window_new (name));

  g_signal_connect (G_OBJECT (window), "delete-event",
      G_CALLBACK (delete_event_cb), NULL);

  g_signal_connect (G_OBJECT (window), "destroy",
      G_CALLBACK (window_destroyed), NULL);

  windows = g_list_prepend (windows, window);
  gtk_widget_show (window);

  return window;
}

int
main (int argc,
      char **argv)
{
  GOptionContext *ctx;
  GError *error = NULL;

  /* Init gettext */
  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  ctx = g_option_context_new ("gnome-sound-recorder");
  /* Initializes gtk during option parsing */
  g_option_context_add_group (ctx, gtk_get_option_group (TRUE));
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_printerr ("Option parsing failed: %s\n", error->message);
    g_error_free (error);
    g_option_context_free (ctx);
    return EXIT_FAILURE;
  }

  g_option_context_free (ctx);
  gtk_window_set_default_icon_name ("gnome-sound-recorder");
  g_setenv ("PULSE_PROP_media.role", "production", TRUE);

  gsr_open_window (NULL);

  gtk_main ();

  return 0;
}
