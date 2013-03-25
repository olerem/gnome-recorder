/*
 *  Authors: Iain Holmes <iain@prettypeople.org>
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
 * permission of Iain Holmes, Ronald Bultje, Leontine Binchy (SUN), Johan Dahlin
 *  and Joe Marcus Clarke
 *
 */

#ifndef __GSR_WINDOW_H__
#define __GSR_WINDOW_H__

#include <gtk/gtk.h>
#include "gsr-gstreamer.h"

#define GSR_TYPE_WINDOW (gsr_window_get_type ())
#define GSR_WINDOW(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSR_TYPE_WINDOW, GSRWindow))
#define GSR_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GSR_TYPE_WINDOW, GSRWindowClass))
#define GSR_IS_WINDOW(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSR_TYPE_WINDOW))
#define GSR_IS_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSR_TYPE_WINDOW))

#define GSR_WINDOW_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), GSR_TYPE_WINDOW, GSRWindowPrivate))

#define EBUSY_TRY_AGAIN     3    /* Empirical data */

struct _GSRWindowPrivate {
  GtkWidget *main_vbox;
  GtkWidget *scale;
  GtkWidget *profile, *input;
  GtkWidget *rate, *time_sec, *format, *channels;
  GtkWidget *input_label;
  GtkWidget *name_label;
  GtkWidget *length_label;
  GtkWidget *align;
  GtkWidget *volume_label;
  GtkWidget *level;

  gulong seek_id;

  GtkUIManager *ui_manager;
  GtkActionGroup *action_group;
  GtkWidget *recent_view;
  GtkRecentFilter *recent_filter;

  /* statusbar */
  GtkWidget *statusbar;
  guint status_message_cid;
  guint tip_message_cid;

  /* Pipelines */
  GSRWindowPipeline *play;
  GSRWindowPipeline *record;
  char *record_filename;
  gchar *audio_path;
  char *filename;
  char *extension;

  int record_fd;

  /* File info */
  int len_secs; /* In seconds */
  int get_length_attempts;

  GDateTime *datetime;

  /* ATOMIC access */
  struct {
    gint n_channels;
    gint bitrate;
    gint samplerate;
  } atomic;

  gboolean has_file;
  gboolean saved;
  gboolean dirty;
  gboolean seek_in_progress;

  gboolean quit_after_save;

  guint32 tick_id; /* tick_callback timeout ID */
  guint32 record_id; /* record idle callback timeout ID */

  GstElement *ebusy_pipeline;  /* which pipeline we're trying to start */
  guint       ebusy_timeout_id;

  GstElement *source;
};


typedef struct _GSRWindow GSRWindow;
typedef struct _GSRWindowClass GSRWindowClass;
typedef struct _GSRWindowPrivate GSRWindowPrivate;

struct _GSRWindow {
  GtkWindow parent;
  GSRWindowPrivate *priv;
};

struct _GSRWindowClass {
  GtkWindowClass parent_class;
};


GType		gsr_window_get_type		(void);

GtkWidget*	gsr_window_new			(const char *filename);
void		gsr_window_close		(GSRWindow *window);
gboolean	gsr_window_is_saved		(GSRWindow *window);
gboolean	gsr_discard_confirmation_dialog	(GSRWindow *window, gboolean closing);

void set_action_sensitive (GSRWindow  *window, const char *name, gboolean sensitive);
void show_error_dialog (GtkWindow *win, const gchar *dbg, const gchar * format, ...);
char * seconds_to_full_string (guint seconds);

#endif
