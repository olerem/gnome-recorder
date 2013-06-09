#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gsr-gstreamer.h"
#include "gsr-window.h"
#include "gsr-fileutil.h"

/* Why do we need this? when a bin changes from READY => NULL state, its
 * bus is set to flushing and we're unlikely to ever see any of its messages
 * if the bin's state reaches NULL before we/the watch in the main thread
 * collects them. That's why we set the state to READY first, process all
 * messages 'manually', and then finally set it to NULL. This makes sure
 * our state-changed handler actually gets to see all the state changes */

static void
gsr_generate_datetime (GSRWindowPrivate *priv)
{
  GDateTime *datetime;

  datetime = g_date_time_new_now_local ();
  g_assert (datetime != NULL);

  if (priv->datetime)
    g_date_time_unref (priv->datetime);

  priv->datetime = g_date_time_ref (datetime);
  g_date_time_unref (datetime);
}

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

void
stop_cb (GtkAction *action,
         GSRWindow *window)
{
  GSRWindowPrivate *priv = window->priv;

  /* Work out what's playing */
  if (priv->play && priv->play->state >= GST_STATE_PAUSED) {
    GST_DEBUG ("Stopping play pipeline");
    set_pipeline_state_to_null (priv->play->pipeline);
  } else if (priv->record && priv->record->state == GST_STATE_PLAYING) {
    GstMessage *msg;

    GST_DEBUG ("Stopping recording source");
    gst_element_send_event (priv->record->src, gst_event_new_eos ());
    /* wait one second for EOS message on the pipeline bus */
    msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (priv->record->pipeline),
        GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    gst_message_unref (msg);

    GST_DEBUG ("Stopping recording pipeline");
    set_pipeline_state_to_null (priv->record->pipeline);
    gtk_widget_set_sensitive (window->priv->level, FALSE);
    gtk_widget_set_sensitive (window->priv->volume_label, FALSE);
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->priv->level), 0.0);
  }
}

static void
pipeline_deep_notify_caps_cb (GstObject  *pipeline,
                              GstObject  *object,
                              GParamSpec *pspec,
                              GSRWindow  *window)
{
  GSRWindowPrivate *priv;
  GstPadDirection direction;

  if (!GST_IS_PAD (object))
    return;

  priv = window->priv;
  if (priv->play && pipeline == GST_OBJECT_CAST (priv->play->pipeline))
    direction = GST_PAD_SRC;
  else if (priv->record &&
           pipeline == GST_OBJECT_CAST (priv->record->pipeline))
    direction = GST_PAD_SINK;
  else
    g_return_if_reached ();


  if (GST_PAD_DIRECTION (object) == direction) {
    GstObject *pad_parent;

    pad_parent = gst_object_get_parent (object);
    if (GST_IS_ELEMENT (pad_parent)) {
      GstElementFactory *factory;
      GstElement *element;
      const gchar *klass;

      element = GST_ELEMENT_CAST (pad_parent);
      if ((factory = gst_element_get_factory (element)) &&
          (klass = gst_element_factory_get_klass (factory)) &&
          strstr (klass, "Audio") &&
          (strstr (klass, "Decoder") || strstr (klass, "Encoder"))) {
        GstCaps *caps;

        caps = gst_pad_get_current_caps (GST_PAD_CAST (object));
        if (caps) {
          GstStructure *s;
          gint val;

          s = gst_caps_get_structure (caps, 0);
          if (gst_structure_get_int (s, "channels", &val))
            g_atomic_int_set (&priv->atomic.n_channels, val);

          if (gst_structure_get_int (s, "rate", &val))
            g_atomic_int_set (&priv->atomic.samplerate, val);

          gst_caps_unref (caps);
        }
      }
    }

    if (pad_parent)
      gst_object_unref (pad_parent);

  }
}

static GstElement *
notgst_element_get_toplevel (GstElement * element)
{
  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  do {
    GstElement *parent;

    parent = (GstElement *) gst_element_get_parent (element);

    if (parent == NULL)
      break;

    gst_object_unref (parent);
    element = parent;
  } while (1);

  return element;
}

static gboolean
handle_ebusy_error (GSRWindow *window)
{
  g_return_val_if_fail (window->priv->ebusy_pipeline != NULL, FALSE);

  gst_element_set_state (window->priv->ebusy_pipeline, GST_STATE_NULL);
  gst_element_get_state (window->priv->ebusy_pipeline, NULL, NULL, -1);
  gst_element_set_state (window->priv->ebusy_pipeline, GST_STATE_PLAYING);

  /* Try only once */
  return FALSE;
}

static void
pipeline_error_cb (GstBus * bus, GstMessage * msg, GSRWindow * window)
{
  GstElement *pipeline;
  GError *error = NULL;
  gchar *dbg = NULL;

  g_return_if_fail (GSR_IS_WINDOW (window));

  gst_message_parse_error (msg, &error, &dbg);
  g_return_if_fail (error != NULL);

  pipeline = notgst_element_get_toplevel (GST_ELEMENT (msg->src));

  if (error->code == GST_RESOURCE_ERROR_BUSY) {
    if (window->priv->ebusy_timeout_id == 0) {
      set_action_sensitive (window, "Play", FALSE);
      set_action_sensitive (window, "Record", FALSE);

      window->priv->ebusy_pipeline = pipeline;

      window->priv->ebusy_timeout_id =
          g_timeout_add_seconds (EBUSY_TRY_AGAIN,
          (GSourceFunc) handle_ebusy_error, window);

      g_error_free (error);
      g_free (dbg);
      return;
    }
  }

  if (window->priv->ebusy_timeout_id) {
    g_source_remove (window->priv->ebusy_timeout_id);
    window->priv->ebusy_timeout_id = 0;
    window->priv->ebusy_pipeline = NULL;
  }

  /* set pipeline to NULL before showing error dialog to make sure
   * the audio device is freed, in case any accessability software
   * wants to make use of it to read out the error message */
  set_pipeline_state_to_null (pipeline);

  show_error_dialog (GTK_WINDOW (window), dbg, "%s", error->message);

  gdk_window_set_cursor (gtk_widget_get_window (window->priv->main_vbox), NULL);

  set_action_sensitive (window, "Stop", FALSE);
  set_action_sensitive (window, "Play", TRUE);
  set_action_sensitive (window, "Record", TRUE);
  gtk_widget_set_sensitive (window->priv->scale, TRUE);

  gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar),
      window->priv->status_message_cid);
  gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar),
      window->priv->status_message_cid, _("Ready"));

  g_error_free (error);
  g_free (dbg);
}

static void
play_eos_msg_cb (GstBus * bus, GstMessage * msg, GSRWindow * window)
{
  g_return_if_fail (GSR_IS_WINDOW (window));

  GST_DEBUG ("EOS");

  stop_cb (NULL, window);
}

static gboolean
play_tick_callback (GSRWindow *window)
{
  GstElement *playbin;
  GstFormat format = GST_FORMAT_TIME;
  gint64 val = -1;

  g_return_val_if_fail (window->priv->play != NULL, FALSE);
  g_return_val_if_fail (window->priv->play->pipeline != NULL, FALSE);

  playbin = window->priv->play->pipeline;

  /* This check stops us from doing an unnecessary query */
  if (window->priv->play->state != GST_STATE_PLAYING) {
    GST_DEBUG ("pipeline in wrong state: %s",
        gst_element_state_get_name (window->priv->play->state));
    window->priv->play->tick_id = 0;
    return FALSE;
  }

  if (gst_element_query_duration (playbin, format, &val) && val != -1) {
    gchar *len_str;

    window->priv->len_secs = val / GST_SECOND;

    len_str = seconds_to_full_string (window->priv->len_secs);
    gtk_label_set_text (GTK_LABEL (window->priv->length_label), len_str);
    g_free (len_str);
  } else {
    if (window->priv->get_length_attempts <= 0) {
      /* Attempts to get length ran out. */
      gtk_label_set_text (GTK_LABEL (window->priv->length_label), _("Unknown"));
    } else
      --window->priv->get_length_attempts;
  }

  if (window->priv->seek_in_progress) {
    GST_DEBUG ("seek in progress, try again later");
    return TRUE;
  }

  if (window->priv->len_secs == 0) {
    GST_DEBUG ("no duration, try again later");
    return TRUE;
  }

  if (gst_element_query_position (playbin, format, &val) && val != -1) {
    gdouble pos, len, percentage;

    pos = (gdouble) (val - (val % GST_SECOND));
    len = (gdouble) window->priv->len_secs * GST_SECOND;
    percentage = pos / len * 100.0;

    gtk_adjustment_set_value (gtk_range_get_adjustment (GTK_RANGE (window->priv->scale)),
        CLAMP (percentage + 0.5, 0.0, 100.0));
  } else
    GST_DEBUG ("failed to query position");

  return TRUE;
}

static void
play_state_changed_cb (GstBus * bus, GstMessage * msg, GSRWindow * window)
{
  GstState new_state;

  gst_message_parse_state_changed (msg, NULL, &new_state, NULL);

  g_return_if_fail (GSR_IS_WINDOW (window));

  /* we are only interested in state changes of the top-level pipeline */
  if (msg->src != GST_OBJECT (window->priv->play->pipeline))
    return;

  window->priv->play->state = new_state;

  GST_DEBUG ("playbin state: %s", gst_element_state_get_name (new_state));

  switch (new_state) {
    case GST_STATE_PLAYING:
      if (window->priv->play->tick_id == 0) {
        window->priv->play->tick_id =
            g_timeout_add (200, (GSourceFunc) play_tick_callback, window);
      }

      set_action_sensitive (window, "Stop", TRUE);
      set_action_sensitive (window, "Play", FALSE);
      set_action_sensitive (window, "Record", FALSE);
      gtk_widget_set_sensitive (window->priv->scale, TRUE);

      gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar),
          window->priv->status_message_cid);
      gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar),
          window->priv->status_message_cid, _("Playing…"));

      if (window->priv->ebusy_timeout_id) {
        g_source_remove (window->priv->ebusy_timeout_id);
        window->priv->ebusy_timeout_id = 0;
        window->priv->ebusy_pipeline = NULL;
      }
      break;
    case GST_STATE_READY:
      if (window->priv->play->tick_id > 0) {
        g_source_remove (window->priv->play->tick_id);
        window->priv->play->tick_id = 0;
      }
      gtk_adjustment_set_value (gtk_range_get_adjustment (GTK_RANGE (window->priv->scale)), 0.0);
      gtk_widget_set_sensitive (window->priv->scale, FALSE);
      /* fallthrough */
    case GST_STATE_PAUSED:
      set_action_sensitive (window, "Stop", FALSE);
      set_action_sensitive (window, "Play", TRUE);
      set_action_sensitive (window, "Record", TRUE);

      gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar),
          window->priv->status_message_cid);
      gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar),
          window->priv->status_message_cid, _("Ready"));
      break;
    default:
      break;
  }
}

GSRWindowPipeline *
make_play_pipeline (GSRWindow *window)
{
  GSRWindowPipeline *obj;
  GstElement *playbin;
  GstElement *audiosink;

  audiosink = gst_element_factory_make ("pulsesink", NULL);
  g_return_val_if_fail (audiosink != NULL, NULL);

  playbin = gst_element_factory_make ("playbin", NULL);
  g_return_val_if_fail (playbin != NULL, NULL);

  obj = g_new0 (GSRWindowPipeline, 1);
  obj->pipeline = playbin;
  obj->src = NULL; /* don't need that for playback */
  obj->sink = NULL; /* don't need that for playback */

  g_object_set (playbin, "audio-sink", audiosink, NULL);

  /* we ultimately want to find out the caps on the decoder's source pad */
  g_signal_connect (playbin, "deep-notify::caps",
      G_CALLBACK (pipeline_deep_notify_caps_cb), window);

  obj->bus = gst_element_get_bus (playbin);

  gst_bus_add_signal_watch_full (obj->bus, G_PRIORITY_HIGH);

  g_signal_connect (obj->bus, "message::state-changed",
      G_CALLBACK (play_state_changed_cb), window);

  g_signal_connect (obj->bus, "message::error",
      G_CALLBACK (pipeline_error_cb), window);

  g_signal_connect (obj->bus, "message::eos",
      G_CALLBACK (play_eos_msg_cb), window);

  return obj;
}

static void
gsr_set_tags (GSRWindow *window)
{
  GSRWindowPrivate *priv = window->priv;
  GstPad *active_pad;
  GstDateTime *gst_datetime;
  GstTagList *taglist;

  g_assert (priv->datetime != NULL);

  gst_datetime = gst_date_time_new_from_g_date_time (priv->datetime);

  taglist = gst_tag_list_new (
      GST_TAG_APPLICATION_NAME, "gnome-sound-recorder",
      GST_TAG_DATE_TIME, gst_datetime,
      GST_TAG_TITLE, priv->record_filename,
      GST_TAG_KEYWORDS, "gnome-sound-recorder", NULL);

  active_pad = gst_element_get_static_pad (priv->record->src, "src");
  gst_pad_push_event (active_pad,
      gst_event_new_tag (gst_tag_list_copy (taglist)));

  gst_date_time_unref (gst_datetime);
}

static gboolean
level_message_handler_cb (GstBus * bus, GstMessage * message, GSRWindow *window)
{

  if (message->type == GST_MESSAGE_ELEMENT) {
    GSRWindowPrivate *priv = window->priv;
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);

    if (g_strcmp0 (name, "level") == 0) {
      gint channels, i;
      gdouble peak_dB;
      gdouble max_peak_dB = -G_MAXDOUBLE;
      gdouble indicator;
      const GValue *array_val;
      const GValueArray *array;

      array_val = gst_structure_get_value (s, "peak");
      array = (GValueArray *) g_value_get_boxed (array_val);
      channels = array->n_values;

      for (i = 0; i < channels; ++i) {
        /* g_value_array_get_nth (array, i) is deprecated
         * so we access array->values directly. */
        peak_dB = g_value_get_double (array->values+i);
        /* we use only biggest valie from different channels
         * and send it to indicator */
        if (max_peak_dB < peak_dB)
          max_peak_dB = peak_dB;
      }

      indicator = exp (max_peak_dB / 20);
      if (indicator > 1.0)
        indicator = 1.0;
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->level), indicator);
    }
  }
  /* we handled the message we want, and ignored the ones we didn't want.
   * so the core can unref the message for us */
  return TRUE;
}

static gboolean
record_tick_callback (GSRWindow *window)
{
  GstElement *pipeline;
  GstFormat format = GST_FORMAT_TIME;
  gint64 val = -1;
  gint secs;

  /* This check stops us from doing an unnecessary query */
  if (window->priv->record->state != GST_STATE_PLAYING) {
    GST_DEBUG ("pipeline in wrong state: %s",
    gst_element_state_get_name (window->priv->record->state));
    return FALSE;
  }

  if (window->priv->seek_in_progress)
    return TRUE;

  pipeline = window->priv->record->pipeline;

  if (gst_element_query_position (pipeline, format, &val) && val != -1) {
    gchar* len_str;

    secs = val / GST_SECOND;
               
    len_str = seconds_to_full_string (secs);
    window->priv->len_secs = secs;
    gtk_label_set_text (GTK_LABEL (window->priv->length_label),
                                   len_str);
    g_free (len_str);
  } else
    GST_DEBUG ("failed to query position");

  return TRUE;
}

static gboolean
record_start (gpointer user_data) 
{
  GSRWindow *window = GSR_WINDOW (user_data);
  gchar *basename;

  g_assert (window->priv->tick_id == 0);

  basename = g_path_get_basename (window->priv->record_filename);
  gtk_label_set_text (GTK_LABEL (window->priv->name_label),
      basename);
  g_free (basename);

  window->priv->get_length_attempts = 16;
  window->priv->tick_id = g_timeout_add (200, (GSourceFunc) record_tick_callback, window);

  set_action_sensitive (window, "Stop", TRUE);
  set_action_sensitive (window, "Play", FALSE);
  set_action_sensitive (window, "Record", FALSE);
  gtk_widget_set_sensitive (window->priv->scale, FALSE);

  gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar),
      window->priv->status_message_cid);
  gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar),
      window->priv->status_message_cid, _("Recording…"));

  window->priv->record_id = 0;

  return FALSE;
}


static void
record_state_changed_cb (GstBus *bus, GstMessage *msg, GSRWindow *window)
{
  GstState  new_state;

  gst_message_parse_state_changed (msg, NULL, &new_state, NULL);

  g_return_if_fail (GSR_IS_WINDOW (window));

  /* we are only interested in state changes of the top-level pipeline */
  if (msg->src != GST_OBJECT (window->priv->record->pipeline))
    return;

  window->priv->record->state = new_state;

  GST_DEBUG ("record pipeline state: %s",
      gst_element_state_get_name (new_state));

  switch (new_state) {
    case GST_STATE_PLAYING:
      window->priv->record_id = g_idle_add (record_start, window);
      break;
    case GST_STATE_READY:
      gtk_adjustment_set_value (gtk_range_get_adjustment (GTK_RANGE (window->priv->scale)), 0.0);
      gtk_widget_set_sensitive (window->priv->scale, FALSE);
      /* fall through */
    case GST_STATE_PAUSED:
      set_action_sensitive (window, "Stop", FALSE);
      set_action_sensitive (window, "Play", TRUE);
      set_action_sensitive (window, "Record", TRUE);
      gtk_widget_set_sensitive (window->priv->scale, FALSE);

      gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar),
          window->priv->status_message_cid);
      gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar),
          window->priv->status_message_cid, _("Ready"));

      if (window->priv->tick_id > 0) {
        g_source_remove (window->priv->tick_id);
        window->priv->tick_id = 0;
      }
      break;
    default:
      break;
  }
}

static void
record_eos_msg_cb (GstBus * bus, GstMessage * msg, GSRWindow * window)
{
  g_return_if_fail (GSR_IS_WINDOW (window));

  GST_DEBUG ("EOS. Finished recording");

  /* FIXME: this was READY before (why?) */
  set_pipeline_state_to_null (window->priv->record->pipeline);

  g_free (window->priv->filename);
  window->priv->filename = g_strdup (window->priv->record_filename);

  window->priv->has_file = TRUE;
}

//done
static GstEncodingProfile *
gsr_profile_get (void)
{
  GstCaps *ogg, *vorbis;
  GstEncodingAudioProfile *a_prof;
  GstEncodingContainerProfile *prof;
  gint ret;

  ogg = gst_caps_new_empty_simple ("application/ogg");
  prof = gst_encoding_container_profile_new (NULL, NULL, ogg, NULL);
  gst_caps_unref (ogg);

  vorbis = gst_caps_new_empty_simple ("audio/x-vorbis");
  a_prof = gst_encoding_audio_profile_new (vorbis, NULL, NULL, 1);
  gst_caps_unref (vorbis);

  ret = gst_encoding_container_profile_add_profile (prof,
      (GstEncodingProfile *) a_prof);
  g_return_val_if_fail(ret, NULL);

  return (GstEncodingProfile *) prof;
}


static GSRWindowPipeline *
make_record_pipeline (GSRWindow *window)
{
  GSRWindowPipeline *pipeline;
  GstElement *source, *level, *encodebin, *filesink;

  source = gst_element_factory_make ("pulsesrc", NULL);
  g_return_val_if_fail (source != NULL, NULL);

  /* Any reason we are not using gnomevfssink here? (tpm) */
  filesink = gst_element_factory_make ("filesink", NULL);
  g_return_val_if_fail (filesink != NULL, NULL);

  pipeline = g_new (GSRWindowPipeline, 1);

  pipeline->pipeline = gst_pipeline_new ("record-pipeline");
  pipeline->src = source;
  pipeline->sink = filesink;

  /* this element should have clearly defined name,
   * this is why we set it here with faktory_make */
  level = gst_element_factory_make ("level", "level");
  g_return_val_if_fail (level != NULL, NULL);

  encodebin = gst_element_factory_make ("encodebin", NULL);
  g_return_val_if_fail (encodebin != NULL, NULL);

  g_object_set (encodebin, "profile", gsr_profile_get(), NULL);
  g_object_set (encodebin, "queue-time-max", 120 * GST_SECOND, NULL);

  gst_bin_add_many (GST_BIN (pipeline->pipeline), source, level,
      encodebin, filesink, NULL);

  if (!gst_element_link_many (source, level, encodebin, filesink, NULL))
      g_return_val_if_reached (NULL);

  /* We ultimately want to find out the caps on the
   * encoder's source pad */
  g_signal_connect (pipeline->pipeline, "deep-notify::caps",
      G_CALLBACK (pipeline_deep_notify_caps_cb), window);

  pipeline->bus = gst_element_get_bus (pipeline->pipeline);

  gst_bus_add_signal_watch (pipeline->bus);

  g_signal_connect (pipeline->bus, "message::element",
      G_CALLBACK (level_message_handler_cb), window);

  g_signal_connect (pipeline->bus, "message::state-changed",
      G_CALLBACK (record_state_changed_cb), window);

  g_signal_connect (pipeline->bus, "message::error",
      G_CALLBACK (pipeline_error_cb), window);

  g_signal_connect (pipeline->bus, "message::eos",
      G_CALLBACK (record_eos_msg_cb), window);

  return pipeline;
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

void
record_cb (GtkAction *action,
           GSRWindow *window)
{
  GSRWindowPrivate *priv = window->priv;

  if (priv->record)
    shutdown_pipeline (priv->record);

  if ((priv->record = make_record_pipeline (window))) {
    priv->len_secs = 0;

    gsr_generate_datetime (priv);
    gsr_filename_from_datetime (priv);

    g_print ("%s", priv->record_filename);
    g_object_set (G_OBJECT (priv->record->sink),
        "location", priv->record_filename, NULL);

    gst_element_set_state (priv->record->pipeline, GST_STATE_PLAYING);
    gsr_set_tags (window);
    gtk_widget_set_sensitive (window->priv->level, TRUE);
    gtk_widget_set_sensitive (window->priv->volume_label, TRUE);

  }
}
