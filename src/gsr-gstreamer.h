#ifndef __GSR_GSTREAMER_H__
#define __GSR_GSTREAMER_H__

typedef struct _GSRWindowPipeline {
  GstElement *pipeline;
  GstState    state;     /* last seen (async) pipeline state */
  GstBus     *bus;

  GstElement *src;
  GstElement *sink;
  GstElement *encodebin;

  guint       tick_id;
} GSRWindowPipeline;

#endif
