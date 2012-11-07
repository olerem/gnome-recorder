#ifndef __GSR_GSTREAMER_H__
#define __GSR_GSTREAMER_H__

#include "gsr-window.h"

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

GSRWindowPipeline * make_record_pipeline (GSRWindow *window);

#endif
