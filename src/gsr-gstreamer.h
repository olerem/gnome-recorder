

typedef struct _GsrPipeline {
  GstElement *pipeline;
  GstState    state;     /* last seen (async) pipeline state */
  GstBus     *bus;

  GstElement *src;
  GstElement *sink;
  GstElement *encodebin;

  guint       tick_id;
} GsrPipeline;

