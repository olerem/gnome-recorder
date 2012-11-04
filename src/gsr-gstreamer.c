


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
      GST_TAG_TITLE, priv->filename,
      GST_TAG_KEYWORDS, "gnome-sound-recorder", NULL);

  active_pad = gst_element_get_static_pad (priv->record->src, "src");
  gst_pad_push_event (active_pad,
      gst_event_new_tag (gst_tag_list_copy (taglist)));

  gst_date_time_unref (gst_datetime);
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
record_cb (GtkAction *action,
           GSRWindow *window)
{
  if (!gsr_window_is_saved(window) &&
      !gsr_discard_confirmation_dialog (window, FALSE))
    return;

  GSRWindowPrivate *priv = window->priv;

  if (priv->record)
    shutdown_pipeline (priv->record);

  if ((priv->record = make_record_pipeline (window))) {
    priv->len_secs = 0;
    priv->saved = FALSE;

    if (priv->filename)
      g_free (priv->filename);

    priv->filename = gsr_generate_filename (window);

    g_print ("%s", priv->record_filename);
    g_object_set (G_OBJECT (priv->record->sink),
        "location", priv->record_filename, NULL);

    gst_element_set_state (priv->record->pipeline, GST_STATE_PLAYING);
    gsr_set_tags (window);
    gtk_widget_set_sensitive (window->priv->level, TRUE);
    gtk_widget_set_sensitive (window->priv->volume_label, TRUE);

  }
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


