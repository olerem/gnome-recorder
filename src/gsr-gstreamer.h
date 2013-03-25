#ifndef __GSR_GSTREAMER_H__
#define __GSR_GSTREAMER_H__

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

typedef struct _GSRWindow GSRWindow;

typedef struct _GSRWindowPipeline {
  GstElement *pipeline;
  GstState    state;     /* last seen (async) pipeline state */
  GstBus     *bus;

  GstElement *src;
  GstElement *sink;
  GstElement *encodebin;

  guint       tick_id;
} GSRWindowPipeline;

void record_cb (GtkAction *action, GSRWindow *window);
GSRWindowPipeline * make_play_pipeline (GSRWindow *window);
void stop_cb (GtkAction *action, GSRWindow *window);

#endif
