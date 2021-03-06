/*
 *  Authors: Iain Holmes <iain@prettypeople.org>
 *           Johan Dahlin <johan@gnome.org>
 *           Tim-Philipp Müller <tim centricular net>
 *
 *  Copyright 2002 Iain Holmes
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
 *
 * 4th Februrary 2005: Christian Schaller: changed license to LGPL with
 * permission of Iain Holmes, Ronald Bultje, Leontine Binchy (SUN), Johan Dalhin
 * and Joe Marcus Clarke
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/gstpad.h>
#include <gst/gstdatetime.h>

#include <gst/pbutils/encoding-profile.h>

#include "gsr-window.h"
#include "gsr-gstreamer.h"
#include "gsr-fileutil.h"

GST_DEBUG_CATEGORY_STATIC (gsr_debug);
#define GST_CAT_DEFAULT gsr_debug

extern GtkWidget * gsr_open_window (const char *filename);
extern void gsr_quit (void);

enum {
  PROP_0,
  PROP_LOCATION
};

static GtkWindowClass *parent_class = NULL;

void
show_error_dialog (GtkWindow   *window,
                   const gchar *debug_message,
                   const gchar *format, ...) G_GNUC_PRINTF (3,4);

void
show_error_dialog (GtkWindow *win, const gchar *dbg, const gchar * format, ...)
{
  GtkWidget *dialog;
  va_list args;
  gchar *s;

  va_start (args, format);
  s = g_strdup_vprintf (format, args);
  va_end (args);

  dialog = gtk_message_dialog_new (win,
				 GTK_DIALOG_DESTROY_WITH_PARENT,
				 GTK_MESSAGE_ERROR,
				 GTK_BUTTONS_CLOSE,
				 "%s",
				 s);

  if (dbg != NULL) {
    g_printerr ("ERROR: %s\nDEBUG MESSAGE: %s\n", s, dbg);
  }

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  g_free (s);
}

/* Why do we need this? when a bin changes from READY => NULL state, its
 * bus is set to flushing and we're unlikely to ever see any of its messages
 * if the bin's state reaches NULL before we/the watch in the main thread
 * collects them. That's why we set the state to READY first, process all
 * messages 'manually', and then finally set it to NULL. This makes sure
 * our state-changed handler actually gets to see all the state changes */

static void
set_pipeline_state_to_null (GstElement *pipeline)
{
  GstMessage *msg;
  GstState cur_state, pending;
  GstBus *bus;

  gst_element_get_state (pipeline, &cur_state, &pending, 0);

  if (cur_state == GST_STATE_NULL && pending == GST_STATE_VOID_PENDING)
    return;

  if (cur_state == GST_STATE_NULL && pending != GST_STATE_VOID_PENDING) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    return;
  }

  gst_element_set_state (pipeline, GST_STATE_READY);
  gst_element_get_state (pipeline, NULL, NULL, -1);

  bus = gst_element_get_bus (pipeline);
  while ((msg = gst_bus_pop (bus))) {
    gst_bus_async_signal_func (bus, msg, NULL);
  }
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  /* maybe we should be paranoid and do _get_state() and check for
   * the return value here, but then errors in shutdown should be
   * rather unlikely */
}


static void
shutdown_pipeline (GSRWindowPipeline *pipe)
{
  gst_bus_set_flushing (pipe->bus, TRUE);
  gst_bus_remove_signal_watch (pipe->bus);
  gst_element_set_state (pipe->pipeline, GST_STATE_NULL);
  gst_object_unref (pipe->pipeline);	
  gst_object_unref (pipe->bus);
}

static char *
seconds_to_string (guint seconds)
{
  int hour, min, sec;

  min = (seconds / 60);
  hour = min / 60;
  min -= (hour * 60);
  sec = seconds - ((hour * 3600) + (min * 60));

  if (hour > 0) {
    return g_strdup_printf ("%d:%02d:%02d", hour, min, sec);
  } else {
    return g_strdup_printf ("%d:%02d", min, sec);
  }
}	

char *
seconds_to_full_string (guint seconds)
{
  long days, hours, minutes;
  char *time = NULL;
  const char *minutefmt;
  const char *hourfmt;
  const char *secondfmt;

  days    = seconds / (60 * 60 * 24);
  hours   = (seconds / (60 * 60));
  minutes = (seconds / 60) - ((days * 24 * 60) + (hours * 60));
  seconds = seconds % 60;

  minutefmt = ngettext ("%ld minute", "%ld minutes", minutes);
  hourfmt = ngettext ("%ld hour", "%ld hours", hours);
  secondfmt = ngettext ("%ld second", "%ld seconds", seconds);

  if (hours > 0) {
    if (minutes > 0)
      if (seconds > 0) {
        char *fmt;
        /* Translators: the format is "X hours, X minutes and X seconds" */
        fmt = g_strdup_printf (_("%s, %s and %s"),
            hourfmt, minutefmt, secondfmt);
        time = g_strdup_printf (fmt, hours, minutes, seconds);
        g_free (fmt);
      } else {
        char *fmt;
	/* Translators: the format is "X hours and X minutes" */
        fmt = g_strdup_printf (_("%s and %s"),
            hourfmt, minutefmt);
	time = g_strdup_printf (fmt, hours, minutes);
        g_free (fmt);
      } else {
        if (seconds > 0) {
          char *fmt;
          /* Translators: the format is "X minutes and X seconds" */
          fmt = g_strdup_printf (_("%s and %s"), minutefmt, secondfmt);
          time = g_strdup_printf (fmt, minutes, seconds);
          g_free (fmt);
        } else
          time = g_strdup_printf (minutefmt, minutes);
      }
  } else {
    if (minutes > 0) {
      if (seconds > 0) {
        char *fmt;
        /* Translators: the format is "X minutes and X seconds" */
        fmt = g_strdup_printf (_("%s and %s"), minutefmt, secondfmt);
        time = g_strdup_printf (fmt, minutes, seconds);
        g_free (fmt);
      } else
        time = g_strdup_printf (minutefmt, minutes);
    } else
      time = g_strdup_printf (secondfmt, seconds);
  }
  return time;
}

void
set_action_sensitive (GSRWindow  *window,
		      const char *name,
		      gboolean    sensitive)
{
  GtkAction *action = gtk_action_group_get_action (window->priv->action_group,
                                                   name);
  gtk_action_set_sensitive (action, sensitive);
}

static void
open_folder_cb (GtkAction *action,
		GSRWindow *window)
{
  GSRWindowPrivate *priv = window->priv;

  GError *error = NULL;
  gchar *uri;

  uri = g_filename_to_uri (priv->audio_path, NULL, NULL);

  gtk_show_uri (gtk_window_get_screen (GTK_WINDOW (window)), uri,
      gtk_get_current_event_time (), &error);

  if (error != NULL) {
    g_warning ("%s", error->message);
    g_error_free (error);
  }
  g_free (uri);
}

static void
run_mixer_cb (GtkAction *action,
	       GSRWindow *window)
{
  char *mixer_path;
  char *argv[4] = {NULL, "sound", "input",  NULL};
  GError *error = NULL;
  gboolean ret;

  /* Open the mixer */
  mixer_path = g_find_program_in_path ("gnome-control-center");
  if (mixer_path == NULL) {
    show_error_dialog (GTK_WINDOW (window), NULL,
        _("%s is not installed in the path."), "gnome-control-center");
    return;
  }

  argv[0] = mixer_path;
  ret = g_spawn_async (NULL, argv, NULL, 0, NULL, NULL, NULL, &error);
  if (ret == FALSE) {
    show_error_dialog (GTK_WINDOW (window), NULL,
        _("There was an error starting %s: %s"), mixer_path, error->message);
    g_error_free (error);
  }

  g_free (mixer_path);
}

static GtkWidget *
make_title_label (const char *text)
{
  GtkWidget *label;
  char *fulltext;
	
  fulltext = g_strdup_printf ("<span weight=\"bold\">%s</span>", text);
  label = gtk_label_new (fulltext);
  g_free (fulltext);

  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.0);
  return label;
}

static GtkWidget *
make_info_label (const char *text)
{
  GtkWidget *label;

  label = gtk_label_new (text);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);
  gtk_label_set_line_wrap (GTK_LABEL (label), GTK_WRAP_WORD);

  return label;
}

static void
pack_table_widget (GtkWidget *table,
		   GtkWidget *widget,
		   int left,
		   int top)
{
  gtk_grid_attach (GTK_GRID (table), widget, left, top, 1, 1);
}

struct _file_props {
  GtkWidget *dialog;

  GtkWidget *dirname;
  GtkWidget *filename;
  GtkWidget *size;
  GtkWidget *length;
  GtkWidget *samplerate;
  GtkWidget *channels;
  GtkWidget *bitrate;
};

static void
fill_in_information (GSRWindow *window,
		     struct _file_props *fp)
{
  struct stat buf;
  guint64 file_size = 0;
  gchar *text, *name;
  gint n_channels, bitrate, samplerate;

  name = g_path_get_dirname (window->priv->record_filename);
  text = g_filename_to_utf8 (name, -1, NULL, NULL, NULL);
  gtk_label_set_text (GTK_LABEL (fp->dirname), text);
  g_free (text);
  g_free (name);

  /* filename */
  gtk_label_set_text (GTK_LABEL (fp->filename), window->priv->basename);

  /* Size */
  if (stat (window->priv->record_filename, &buf) == 0) {
    gchar *human;

    file_size = (guint64) buf.st_size;
    human = g_format_size (file_size);

    text = g_strdup_printf (ngettext ("%s (%" G_GINT64_FORMAT " byte)",
        "%s (%" G_GINT64_FORMAT " bytes)", file_size), human, file_size);
    g_free (human);
  } else
    text = g_strdup (_("Unknown size"));

  gtk_label_set_text (GTK_LABEL (fp->size), text);
  g_free (text);

  /* FIXME: Set up and run our own pipeline 
   *        till we can get the info */
  /* Length */
  if (window->priv->len_secs == 0)
    text = g_strdup (_("Unknown"));
  else
    text = seconds_to_full_string (window->priv->len_secs);

  gtk_label_set_text (GTK_LABEL (fp->length), text);
  g_free (text);

  /* sample rate */
  samplerate = g_atomic_int_get (&window->priv->atomic.samplerate);
  if (samplerate == 0)
    text = g_strdup (_("Unknown"));
  else
    text = g_strdup_printf (_("%.1f kHz"), (float) samplerate / 1000);

  gtk_label_set_text (GTK_LABEL (fp->samplerate), text);
  g_free (text);

  /* bit rate */
  bitrate = g_atomic_int_get (&window->priv->atomic.bitrate);
  if (bitrate > 0)
    text = g_strdup_printf (_("%.0f kb/s"), (float) bitrate / 1000);
  else if (window->priv->len_secs > 0 && file_size > 0) {
    bitrate = (file_size * 8.0) / window->priv->len_secs;
    text = g_strdup_printf (_("%.0f kb/s (Estimated)"),
        (float) bitrate / 1000);
  } else
    text = g_strdup (_("Unknown"));

  gtk_label_set_text (GTK_LABEL (fp->bitrate), text);
  g_free (text);

  /* channels */
  n_channels = g_atomic_int_get (&window->priv->atomic.n_channels);
  switch (n_channels) {
    case 0:
      text = g_strdup (_("Unknown"));
      break;
    case 1:
      text = g_strdup (_("1 (mono)"));
      break;
    case 2:
      text = g_strdup (_("2 (stereo)"));
      break;
    default:
      text = g_strdup_printf ("%d", n_channels);
      break;
  }
  gtk_label_set_text (GTK_LABEL (fp->channels), text);
  g_free (text);
}

static void
dialog_closed_cb (GtkDialog *dialog,
		  guint response_id,
		  struct _file_props *fp)
{
  gtk_widget_destroy (fp->dialog);
  g_free (fp);
}

static void
file_properties_cb (GtkAction *action,
		    GSRWindow *window)
{
  GtkWidget *dialog, *vbox, *inner_vbox, *hbox, *table, *label;
  char *title;
  struct _file_props *fp;
  title = g_strdup_printf (_("%s Information"), window->priv->basename);

  dialog = gtk_dialog_new_with_buttons (title, GTK_WINDOW (window),
      GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
  g_free (title);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  gtk_box_set_spacing (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 2);
  fp = g_new (struct _file_props, 1);
  fp->dialog = dialog;

  g_signal_connect (G_OBJECT (dialog), "response",
      G_CALLBACK (dialog_closed_cb), fp);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox, TRUE, TRUE, 0);

  inner_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (vbox), inner_vbox, FALSE, FALSE,0);

  label = make_title_label (_("File Information"));
  gtk_box_pack_start (GTK_BOX (inner_vbox), label, FALSE, FALSE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (inner_vbox), hbox, TRUE, TRUE, 0);

  label = gtk_label_new ("    ");
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

  /* File properties */
  table = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (table), 12);
  gtk_grid_set_row_spacing (GTK_GRID (table), 6);
  gtk_box_pack_start (GTK_BOX (hbox), table, TRUE, TRUE, 0);

  label = make_info_label (_("Folder:"));
  pack_table_widget (table, label, 0, 0);

  fp->dirname = make_info_label ("");
  pack_table_widget (table, fp->dirname, 1, 0);

  label = make_info_label (_("Filename:"));
  pack_table_widget (table, label, 0, 1);

  fp->filename = make_info_label ("");
  pack_table_widget (table, fp->filename, 1, 1);

  label = make_info_label (_("File size:"));
  pack_table_widget (table, label, 0, 2);

  fp->size = make_info_label ("");
  pack_table_widget (table, fp->size, 1, 2);

  inner_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (vbox), inner_vbox, FALSE, FALSE, 0);

  label = make_title_label (_("Audio Information"));
  gtk_box_pack_start (GTK_BOX (inner_vbox), label, FALSE, FALSE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (inner_vbox), hbox, TRUE, TRUE, 0);

  label = gtk_label_new ("    ");
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

  /* Audio info */
  table = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (table), 12);
  gtk_grid_set_row_spacing (GTK_GRID (table), 6);
  gtk_box_pack_start (GTK_BOX (hbox), table, TRUE, TRUE, 0);

  label = make_info_label (_("File duration:"));
  pack_table_widget (table, label, 0, 0);

  fp->length = make_info_label ("");
  pack_table_widget (table, fp->length, 1, 0);

  label = make_info_label (_("Number of channels:"));
  pack_table_widget (table, label, 0, 1);

  fp->channels = make_info_label ("");
  pack_table_widget (table, fp->channels, 1, 1);

  label = make_info_label (_("Sample rate:"));
  pack_table_widget (table, label, 0, 2);

  fp->samplerate = make_info_label ("");
  pack_table_widget (table, fp->samplerate, 1, 2);

  label = make_info_label (_("Bit rate:"));
  pack_table_widget (table, label, 0, 3);

  fp->bitrate = make_info_label ("");
  pack_table_widget (table, fp->bitrate, 1, 3);

  fill_in_information (window, fp);
  gtk_widget_show_all (dialog);
}

void
gsr_window_close (GSRWindow *window)
{
  gtk_widget_destroy (GTK_WIDGET (window));
}

static void
quit_cb (GtkAction *action,
	 GSRWindow *window)
{
  gsr_quit ();
}

static void
help_contents_cb (GtkAction *action,
	          GSRWindow *window)
{
  GError *error = NULL;

  gtk_show_uri (gtk_window_get_screen (GTK_WINDOW (window)),
     "ghelp:gnome-sound-recorder",
      gtk_get_current_event_time (), &error);

  if (error != NULL) {
    g_warning ("%s", error->message);
    g_error_free (error);
  }
}

static void
about_cb (GtkAction *action,
	  GSRWindow *window)
{
  const char * const authors[] = {"Iain Holmes <iain@prettypeople.org>",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>",
      "Johan Dahlin  <johan@gnome.org>",
      "Tim-Philipp M\303\274ller <tim centricular net>", NULL};

  const char * const documenters[] = {"Sun Microsystems", NULL};
 
  gtk_show_about_dialog (GTK_WINDOW (window),
      "name", _("Sound Recorder"),
      "version", VERSION,
      "copyright", "Copyright \xc2\xa9 2002 Iain Holmes",
      "comments", _("A sound recorder for GNOME\n gnome-multimedia@gnome.org"),
      "authors", authors,
      "documenters", documenters,
      "logo-icon-name", "gnome-sound-recorder", NULL);
}

static void
play_cb (GtkAction *action,
	 GSRWindow *window)
{
  GSRWindowPrivate *priv = window->priv;

  if (!priv->record_filename)
    return;

  if (priv->play)
    shutdown_pipeline (priv->play);

  if ((priv->play = make_play_pipeline (window))) {
    gchar *uri;

    uri = g_filename_to_uri (priv->record_filename, NULL, NULL);
    g_object_set (window->priv->play->pipeline, "uri", uri, NULL);
    g_free (uri);

    if (priv->record && priv->record->state == GST_STATE_PLAYING)
      set_pipeline_state_to_null (priv->record->pipeline);

    gst_element_set_state (priv->play->pipeline, GST_STATE_PLAYING);
  }
}

static gboolean
seek_started (GtkRange *range,
	      GdkEventButton *event,
	      GSRWindow *window)
{
  g_return_val_if_fail (window->priv != NULL, FALSE);

  window->priv->seek_in_progress = TRUE;
  return FALSE;
}

static gboolean
seek_to (GtkRange *range,
	 GdkEventButton *gdkevent,
	 GSRWindow *window)
{
  gdouble value;
  gint64 time;

  if (window->priv->play->state < GST_STATE_PLAYING)
    return FALSE;

  value = gtk_adjustment_get_value (gtk_range_get_adjustment (range));
  time = ((value / 100.0) * window->priv->len_secs) * GST_SECOND;

  gst_element_seek (window->priv->play->pipeline, 1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, time,
      GST_SEEK_TYPE_NONE, 0);

  window->priv->seek_in_progress = FALSE;

  return FALSE;
}

static char *
calculate_format_value (GtkScale *scale,
			double value,
			GSRWindow *window)
{
  gint seconds;

  if (window->priv->record &&
      window->priv->record->state == GST_STATE_PLAYING) {
    seconds = value;
    return seconds_to_string (seconds);
  } else {
    seconds = window->priv->len_secs * (value / 100);
    return seconds_to_string (seconds);
  }
}
	
static const GtkActionEntry menu_entries[] =
{
  /* File menu.  */
  { "File", NULL, N_("_File") },
  { "OpenFolder", GTK_STOCK_EXECUTE, N_("Open Folder"), NULL,
      N_("Open folder with records"), G_CALLBACK (open_folder_cb) },
  { "RunMixer", GTK_STOCK_EXECUTE, N_("Open Volu_me Control"), NULL,
      N_("Open the audio mixer"), G_CALLBACK (run_mixer_cb) },
  { "FileProperties", GTK_STOCK_PROPERTIES, NULL, "<control>I",
      N_("Show information about the current file"),
      G_CALLBACK (file_properties_cb) },
  { "Quit", GTK_STOCK_QUIT, NULL, NULL,
      N_("Quit the program"), G_CALLBACK (quit_cb) },

  /* Control menu */
  { "Control", NULL, N_("_Control") },
  { "Record", GTK_STOCK_MEDIA_RECORD, NULL, "<control>R",
      N_("Record sound"), G_CALLBACK (record_cb) },
  { "Play", GTK_STOCK_MEDIA_PLAY, NULL, "<control>P",
      N_("Play sound"), G_CALLBACK (play_cb) },
  { "Stop", GTK_STOCK_MEDIA_STOP, NULL, "<control>X",
      N_("Stop sound"), G_CALLBACK (stop_cb) },

  /* Help menu */
  { "Help", NULL, N_("_Help") },
  {"HelpContents", GTK_STOCK_HELP, N_("Contents"), "F1",
      N_("Open the manual"), G_CALLBACK (help_contents_cb) },
  { "About", GTK_STOCK_ABOUT, NULL, NULL,
      N_("About this application"), G_CALLBACK (about_cb) }
};

static void
menu_item_select_cb (GtkMenuItem *proxy,
                     GSRWindow *window)
{
  GtkAction *action;
  char *message;

  action = g_object_get_data (G_OBJECT (proxy),  "gtk-action");
  g_return_if_fail (action != NULL);

  g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
  if (message) {
    gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar),
        window->priv->tip_message_cid, message);
    g_free (message);
  }
}

static void
menu_item_deselect_cb (GtkMenuItem *proxy,
                       GSRWindow *window)
{
  gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar),
      window->priv->tip_message_cid);
}
 
static void
connect_proxy_cb (GtkUIManager *manager,
                  GtkAction *action,
                  GtkWidget *proxy,
                  GSRWindow *window)
{
  if (GTK_IS_MENU_ITEM (proxy)) {
    g_signal_connect (proxy, "select",
        G_CALLBACK (menu_item_select_cb), window);
    g_signal_connect (proxy, "deselect",
        G_CALLBACK (menu_item_deselect_cb), window);
  }
}

static void
disconnect_proxy_cb (GtkUIManager *manager,
                     GtkAction *action,
                     GtkWidget *proxy,
                     GSRWindow *window)
{
  if (GTK_IS_MENU_ITEM (proxy)) {
    g_signal_handlers_disconnect_by_func
        (proxy, G_CALLBACK (menu_item_select_cb), window);
    g_signal_handlers_disconnect_by_func
        (proxy, G_CALLBACK (menu_item_deselect_cb), window);
  }
}

/* find the given filename in the uninstalled or installed ui dir */
static gchar *
find_ui_file (const gchar * filename)
{
  gchar * path;

  path = g_build_filename (GSR_UIDIR_UNINSTALLED, filename, NULL);
  if (g_file_test (path, G_FILE_TEST_EXISTS))
    return path;

  g_free (path);
  path = g_build_filename (GSR_UIDIR, filename, NULL);
  if (g_file_test (path, G_FILE_TEST_EXISTS))
    return path;

  g_free (path);
  return NULL;
}

static void
gsr_window_init (GSRWindow *window)
{
  GSRWindowPrivate *priv;
  GError *error = NULL;
  GtkWidget *main_vbox;
  GtkWidget *menubar;
  GtkWidget *toolbar;
  GtkWidget *content_vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *table;
  GtkWidget *align;
  GtkWidget *frame;
  gchar *path;
  GtkShadowType shadow_type;
  window->priv = GSR_WINDOW_GET_PRIVATE (window);
  priv = window->priv;

  gsr_set_audio_path (priv);

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (window), main_vbox);
  priv->main_vbox = main_vbox;
  gtk_widget_show (main_vbox);

  /* menu & toolbar */
  priv->ui_manager = gtk_ui_manager_new ();

  gtk_window_add_accel_group (GTK_WINDOW (window),
      gtk_ui_manager_get_accel_group (priv->ui_manager));

  path = find_ui_file ("ui.xml");
  gtk_ui_manager_add_ui_from_file (priv->ui_manager, path, &error);

  if (error != NULL) {
    show_error_dialog (GTK_WINDOW (window), error->message,
        _("Could not load UI file. "
          "The program may not be properly installed."));
    g_error_free (error);
    exit (1);
  }
  g_free (path);

  /* show tooltips in the statusbar */
  g_signal_connect (priv->ui_manager, "connect_proxy",
      G_CALLBACK (connect_proxy_cb), window);
  g_signal_connect (priv->ui_manager, "disconnect_proxy",
      G_CALLBACK (disconnect_proxy_cb), window);

  priv->action_group = gtk_action_group_new ("GSRWindowActions");
  gtk_action_group_set_translation_domain (priv->action_group, NULL);
  gtk_action_group_add_actions (priv->action_group,
      menu_entries, G_N_ELEMENTS (menu_entries), window);

  gtk_ui_manager_insert_action_group (priv->ui_manager, priv->action_group, 0);

  /* set short labels to use in the toolbar */
  set_action_sensitive (window, "Play", FALSE);
  set_action_sensitive (window, "Stop", FALSE);

  menubar = gtk_ui_manager_get_widget (priv->ui_manager, "/MenuBar");
  gtk_box_pack_start (GTK_BOX (main_vbox), menubar, FALSE, FALSE, 0);
  gtk_widget_show (menubar);

  toolbar = gtk_ui_manager_get_widget (priv->ui_manager, "/ToolBar");
  gtk_toolbar_set_show_arrow (GTK_TOOLBAR (toolbar), FALSE);
  gtk_box_pack_start (GTK_BOX (main_vbox), toolbar, FALSE, FALSE, 0);
  gtk_widget_show (toolbar);

  /* window content: hscale, labels, etc */
  content_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 7);
  gtk_container_set_border_width (GTK_CONTAINER (content_vbox), 6);
  gtk_box_pack_start (GTK_BOX (main_vbox), content_vbox, TRUE, TRUE, 0);
  gtk_widget_show (content_vbox);

  priv->scale = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL,
      GTK_ADJUSTMENT (gtk_adjustment_new (0, 0, 100, 1, 1, 0)));
  priv->seek_in_progress = FALSE;
  g_signal_connect (priv->scale, "format-value",
      G_CALLBACK (calculate_format_value), window);
  g_signal_connect (priv->scale, "button-press-event",
      G_CALLBACK (seek_started), window);
  g_signal_connect (priv->scale, "button-release-event",
      G_CALLBACK (seek_to), window);

  gtk_scale_set_value_pos (GTK_SCALE (window->priv->scale), GTK_POS_BOTTOM);
  /* We can't seek until we find out the length */
  gtk_widget_set_sensitive (window->priv->scale, FALSE);
  gtk_box_pack_start (GTK_BOX (content_vbox), priv->scale, FALSE, FALSE, 6);
  gtk_widget_show (window->priv->scale);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (content_vbox), hbox, FALSE, FALSE, 0);

  label = gtk_label_new ("    "); /* FIXME: better padding? */
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

  table = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (table), 12);
  gtk_grid_set_row_spacing (GTK_GRID (table), 6);
  gtk_box_pack_start (GTK_BOX (hbox), table, TRUE, TRUE, 0);

  label = make_title_label (_("File Information"));

  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_grid_attach (GTK_GRID (table), label, 0, 0, 2, 1);

  label = gtk_label_new (_("Filename:"));

  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_grid_attach (GTK_GRID (table), label, 0, 1, 1, 1);

  priv->name_label = gtk_label_new (_("<none>"));
  gtk_label_set_selectable (GTK_LABEL (priv->name_label), TRUE);
  gtk_label_set_line_wrap (GTK_LABEL (priv->name_label), GTK_WRAP_WORD);
  gtk_misc_set_alignment (GTK_MISC (priv->name_label), 0, 0.5);
  gtk_grid_attach (GTK_GRID (table), priv->name_label, 1, 1, 1, 1);

  atk_object_add_relationship (gtk_widget_get_accessible (GTK_WIDGET (priv->name_label)),
      ATK_RELATION_LABELLED_BY,
      gtk_widget_get_accessible (GTK_WIDGET (label)));


  label = gtk_label_new (_("Length:"));

  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_grid_attach (GTK_GRID (table), label, 0, 2, 1, 1);

  priv->length_label = gtk_label_new ("");
  gtk_label_set_selectable (GTK_LABEL (priv->length_label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (priv->length_label), 0, 0.5);
  gtk_grid_attach (GTK_GRID (table), priv->length_label, 1, 2, 1, 1);

  atk_object_add_relationship (gtk_widget_get_accessible (GTK_WIDGET (priv->length_label)),
      ATK_RELATION_LABELLED_BY,
      gtk_widget_get_accessible (GTK_WIDGET (label)));

  /* statusbar */
  priv->statusbar = gtk_statusbar_new ();
  gtk_widget_set_can_focus (priv->statusbar, TRUE);
  gtk_box_pack_end (GTK_BOX (main_vbox), priv->statusbar, FALSE, FALSE, 0);
  gtk_widget_show (priv->statusbar);

  /* hack to get the same shadow as the status bar.. */
  gtk_widget_style_get (GTK_WIDGET (priv->statusbar), "shadow-type",
      &shadow_type, NULL);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), shadow_type);
  gtk_widget_show (frame);

  gtk_box_pack_end (GTK_BOX (priv->statusbar), frame, FALSE, TRUE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add (GTK_CONTAINER (frame), hbox);
  gtk_box_set_spacing (GTK_BOX (hbox), 6);

  priv->volume_label = gtk_label_new (_("Level:"));
  gtk_box_pack_start (GTK_BOX (hbox), priv->volume_label, FALSE, TRUE, 0);

  /* initialize priv->level */
  align = gtk_aspect_frame_new ("", 0.0, 0.0, 20, FALSE);
  gtk_frame_set_shadow_type (GTK_FRAME (align), GTK_SHADOW_NONE);
  gtk_widget_show (align);
  gtk_box_pack_start (GTK_BOX (hbox), align, FALSE, FALSE, 0);

  priv->level = gtk_progress_bar_new ();
  gtk_container_add (GTK_CONTAINER (align), priv->level);

  gtk_widget_set_sensitive (window->priv->volume_label, FALSE);
  gtk_widget_set_sensitive (window->priv->level, FALSE);

  priv->status_message_cid = gtk_statusbar_get_context_id
      (GTK_STATUSBAR (priv->statusbar), "status_message");
  priv->tip_message_cid = gtk_statusbar_get_context_id
      (GTK_STATUSBAR (priv->statusbar), "tip_message");

  gtk_statusbar_push (GTK_STATUSBAR (priv->statusbar),
      priv->status_message_cid, _("Ready"));

  gtk_widget_show_all (main_vbox);

  /* Make the pipelines */
  priv->play = NULL;
  priv->record = NULL;

  priv->len_secs = 0;
  priv->get_length_attempts = 16;
  priv->dirty = TRUE;
}

static void
gsr_window_finalize (GObject *object)
{
  GSRWindow *window;
  GSRWindowPrivate *priv;

  window = GSR_WINDOW (object);
  priv = window->priv;

  GST_DEBUG ("finalizing ...");

  if (priv == NULL)
    return;

  if (priv->ui_manager) {
    g_object_unref (priv->ui_manager);
    priv->ui_manager = NULL;
  }

  if (priv->action_group) {
    g_object_unref (priv->action_group);
    priv->action_group = NULL;
  }

  if (priv->tick_id > 0) {
    g_source_remove (priv->tick_id);
    window->priv->play->tick_id = 0;
  }

  if (priv->record_id > 0) {
    g_source_remove (priv->record_id);
  }

  if (priv->ebusy_timeout_id > 0) {
    g_source_remove (window->priv->ebusy_timeout_id);
  }

  g_idle_remove_by_data (window);

  if (priv->play != NULL) {
    shutdown_pipeline (priv->play);
    g_free (priv->play);
  }

  if (priv->record != NULL) {
    shutdown_pipeline (priv->record);
    g_free (priv->record);
  }

  g_free (priv->record_filename);

  G_OBJECT_CLASS (parent_class)->finalize (object);

  window->priv = NULL;
}

static void
gsr_window_class_init (GSRWindowClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gsr_window_finalize;

  parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (object_class, sizeof (GSRWindowPrivate));

  GST_DEBUG_CATEGORY_INIT (gsr_debug, "gsr", 0, "Gnome Recorder");
}

GType
gsr_window_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0)) {
    GTypeInfo info = {
        sizeof (GSRWindowClass),
        NULL, NULL,
        (GClassInitFunc) gsr_window_class_init,
        NULL, NULL,
        sizeof (GSRWindow), 0,
        (GInstanceInitFunc) gsr_window_init
    };

  type = g_type_register_static (GTK_TYPE_WINDOW,
      "GSRWindow", &info, 0);
  }

  return type;
}

GtkWidget *
gsr_window_new (void)
{
  GSRWindow *window;

  window = g_object_new (GSR_TYPE_WINDOW, NULL);

  window->priv->has_file = FALSE;

  gtk_window_set_default_size (GTK_WINDOW (window), 512, 200);
  gtk_window_set_title (GTK_WINDOW (window), "Sound Recorder");

  return GTK_WIDGET (window);
}
