/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <pulse/pulseaudio.h>

#include "gvc-mixer-stream.h"

#define GVC_MIXER_STREAM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GVC_TYPE_MIXER_STREAM, GvcMixerStreamPrivate))

static guint32 stream_serial = 1;

struct GvcMixerStreamPrivate
{
        pa_context    *pa_context;
        guint          id;
        guint          index;
        guint          num_channels;
        guint          volume;
        char          *name;
        char          *icon_name;
        gboolean       is_muted;
        gboolean       is_default;
};

enum
{
        PROP_0,
        PROP_ID,
        PROP_PA_CONTEXT,
        PROP_NUM_CHANNELS,
        PROP_INDEX,
        PROP_NAME,
        PROP_ICON_NAME,
        PROP_VOLUME,
        PROP_IS_MUTED,
        PROP_IS_DEFAULT,
};

static void     gvc_mixer_stream_class_init (GvcMixerStreamClass *klass);
static void     gvc_mixer_stream_init       (GvcMixerStream      *mixer_stream);
static void     gvc_mixer_stream_finalize   (GObject            *object);

G_DEFINE_ABSTRACT_TYPE (GvcMixerStream, gvc_mixer_stream, G_TYPE_OBJECT)

static guint32
get_next_stream_serial (void)
{
        guint32 serial;

        serial = stream_serial++;

        if ((gint32)stream_serial < 0) {
                stream_serial = 1;
        }

        return serial;
}

pa_context *
gvc_mixer_stream_get_pa_context (GvcMixerStream *stream)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), 0);
        return stream->priv->pa_context;
}

guint
gvc_mixer_stream_get_index (GvcMixerStream *stream)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), 0);
        return stream->priv->index;
}

guint
gvc_mixer_stream_get_id (GvcMixerStream *stream)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), 0);
        return stream->priv->id;
}

guint
gvc_mixer_stream_get_num_channels (GvcMixerStream *stream)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), 0);
        return stream->priv->num_channels;
}

guint
gvc_mixer_stream_get_volume (GvcMixerStream *stream)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), 0);
        return stream->priv->volume;
}

gboolean
gvc_mixer_stream_set_volume (GvcMixerStream *stream,
                             pa_volume_t     volume)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), FALSE);

        if (volume != stream->priv->volume) {
                stream->priv->volume = volume;
                g_object_notify (G_OBJECT (stream), "volume");
        }

        return TRUE;
}

gboolean
gvc_mixer_stream_get_is_muted  (GvcMixerStream *stream)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), FALSE);
        return stream->priv->is_muted;
}

gboolean
gvc_mixer_stream_get_is_default  (GvcMixerStream *stream)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), FALSE);
        return stream->priv->is_default;
}

gboolean
gvc_mixer_stream_set_is_muted  (GvcMixerStream *stream,
                                gboolean        is_muted)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), FALSE);

        if (is_muted != stream->priv->is_muted) {
                stream->priv->is_muted = is_muted;
                g_object_notify (G_OBJECT (stream), "is-muted");
        }

        return TRUE;
}

gboolean
gvc_mixer_stream_set_is_default  (GvcMixerStream *stream,
                                  gboolean        is_default)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), FALSE);

        if (is_default != stream->priv->is_default) {
                stream->priv->is_default = is_default;
                g_object_notify (G_OBJECT (stream), "is-default");
        }

        return TRUE;
}

char *
gvc_mixer_stream_get_name (GvcMixerStream *stream)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), NULL);
        return stream->priv->name;
}

gboolean
gvc_mixer_stream_set_name (GvcMixerStream *stream,
                           const char     *name)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), FALSE);

        g_free (stream->priv->name);
        stream->priv->name = g_strdup (name);
        g_object_notify (G_OBJECT (stream), "name");

        return TRUE;
}

char *
gvc_mixer_stream_get_icon_name (GvcMixerStream *stream)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), NULL);
        return stream->priv->icon_name;
}

gboolean
gvc_mixer_stream_set_icon_name (GvcMixerStream *stream,
                                const char     *icon_name)
{
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), FALSE);

        g_free (stream->priv->icon_name);
        stream->priv->icon_name = g_strdup (icon_name);
        g_object_notify (G_OBJECT (stream), "icon-name");

        return TRUE;
}

static void
gvc_mixer_stream_set_property (GObject       *object,
                               guint          prop_id,
                               const GValue  *value,
                               GParamSpec    *pspec)
{
        GvcMixerStream *self = GVC_MIXER_STREAM (object);

        switch (prop_id) {
        case PROP_PA_CONTEXT:
                self->priv->pa_context = g_value_get_pointer (value);
                break;
        case PROP_INDEX:
                self->priv->index = g_value_get_ulong (value);
                break;
        case PROP_ID:
                self->priv->id = g_value_get_ulong (value);
                break;
        case PROP_NUM_CHANNELS:
                self->priv->num_channels = g_value_get_ulong (value);
                break;
        case PROP_NAME:
                gvc_mixer_stream_set_name (self, g_value_get_string (value));
                break;
        case PROP_ICON_NAME:
                gvc_mixer_stream_set_icon_name (self, g_value_get_string (value));
                break;
        case PROP_VOLUME:
                gvc_mixer_stream_set_volume (self, g_value_get_ulong (value));
                break;
        case PROP_IS_MUTED:
                gvc_mixer_stream_set_is_muted (self, g_value_get_boolean (value));
                break;
        case PROP_IS_DEFAULT:
                gvc_mixer_stream_set_is_default (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gvc_mixer_stream_get_property (GObject     *object,
                               guint        prop_id,
                               GValue      *value,
                               GParamSpec  *pspec)
{
        GvcMixerStream *self = GVC_MIXER_STREAM (object);

        switch (prop_id) {
        case PROP_PA_CONTEXT:
                g_value_set_pointer (value, self->priv->pa_context);
                break;
        case PROP_INDEX:
                g_value_set_ulong (value, self->priv->index);
                break;
        case PROP_ID:
                g_value_set_ulong (value, self->priv->id);
                break;
        case PROP_NUM_CHANNELS:
                g_value_set_ulong (value, self->priv->num_channels);
                break;
        case PROP_NAME:
                g_value_set_string (value, self->priv->name);
                break;
        case PROP_ICON_NAME:
                g_value_set_string (value, self->priv->icon_name);
                break;
        case PROP_VOLUME:
                g_value_set_ulong (value, self->priv->volume);
                break;
        case PROP_IS_MUTED:
                g_value_set_boolean (value, self->priv->is_muted);
                break;
        case PROP_IS_DEFAULT:
                g_value_set_boolean (value, self->priv->is_default);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gvc_mixer_stream_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_params)
{
        GObject       *object;
        GvcMixerStream *self;

        object = G_OBJECT_CLASS (gvc_mixer_stream_parent_class)->constructor (type, n_construct_properties, construct_params);

        self = GVC_MIXER_STREAM (object);

        self->priv->id = get_next_stream_serial ();

        return object;
}

static gboolean
gvc_mixer_stream_real_change_volume (GvcMixerStream *stream,
                                     guint           volume)
{
        return FALSE;
}

static gboolean
gvc_mixer_stream_real_change_is_muted (GvcMixerStream *stream,
                                       gboolean        is_muted)
{
        return FALSE;
}

gboolean
gvc_mixer_stream_change_volume (GvcMixerStream *stream,
                                guint           volume)
{
        gboolean ret;
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), FALSE);
        ret = GVC_MIXER_STREAM_GET_CLASS (stream)->change_volume (stream, volume);
        return ret;
}

gboolean
gvc_mixer_stream_change_is_muted (GvcMixerStream *stream,
                                  gboolean        is_muted)
{
        gboolean ret;
        g_return_val_if_fail (GVC_IS_MIXER_STREAM (stream), FALSE);
        ret = GVC_MIXER_STREAM_GET_CLASS (stream)->change_is_muted (stream, is_muted);
        return ret;
}

static void
gvc_mixer_stream_class_init (GvcMixerStreamClass *klass)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (klass);

        gobject_class->constructor = gvc_mixer_stream_constructor;
        gobject_class->finalize = gvc_mixer_stream_finalize;
        gobject_class->set_property = gvc_mixer_stream_set_property;
        gobject_class->get_property = gvc_mixer_stream_get_property;

        klass->change_volume = gvc_mixer_stream_real_change_volume;
        klass->change_is_muted = gvc_mixer_stream_real_change_is_muted;

        g_object_class_install_property (gobject_class,
                                         PROP_INDEX,
                                         g_param_spec_ulong ("index",
                                                             "Index",
                                                             "The index for this stream",
                                                             0, G_MAXULONG, 0,
                                                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (gobject_class,
                                         PROP_ID,
                                         g_param_spec_ulong ("id",
                                                             "id",
                                                             "The id for this stream",
                                                             0, G_MAXULONG, 0,
                                                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (gobject_class,
                                         PROP_NUM_CHANNELS,
                                         g_param_spec_ulong ("num-channels",
                                                             "num channels",
                                                             "The number of channels for this stream",
                                                             0, G_MAXULONG, 0,
                                                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (gobject_class,
                                         PROP_PA_CONTEXT,
                                         g_param_spec_pointer ("pa-context",
                                                               "PulseAudio context",
                                                               "The PulseAudio context for this stream",
                                                               G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (gobject_class,
                                         PROP_VOLUME,
                                         g_param_spec_ulong ("volume",
                                                             "Volume",
                                                             "The volume for this stream",
                                                             0, G_MAXULONG, 0,
                                                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

        g_object_class_install_property (gobject_class,
                                         PROP_NAME,
                                         g_param_spec_string ("name",
                                                              "Name",
                                                              "Name to display for this stream",
                                                              NULL,
                                                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT));
        g_object_class_install_property (gobject_class,
                                         PROP_ICON_NAME,
                                         g_param_spec_string ("icon-name",
                                                              "Icon Name",
                                                              "Name of icon to display for this stream",
                                                              NULL,
                                                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT));
        g_object_class_install_property (gobject_class,
                                         PROP_IS_MUTED,
                                         g_param_spec_boolean ("is-muted",
                                                               "is muted",
                                                               "Whether stream is muted",
                                                               FALSE,
                                                               G_PARAM_READWRITE|G_PARAM_CONSTRUCT));
        g_object_class_install_property (gobject_class,
                                         PROP_IS_DEFAULT,
                                         g_param_spec_boolean ("is-default",
                                                               "is default",
                                                               "Whether stream is the default",
                                                               FALSE,
                                                               G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GvcMixerStreamPrivate));
}

static void
gvc_mixer_stream_init (GvcMixerStream *stream)
{
        stream->priv = GVC_MIXER_STREAM_GET_PRIVATE (stream);

}

static void
gvc_mixer_stream_finalize (GObject *object)
{
        GvcMixerStream *mixer_stream;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GVC_IS_MIXER_STREAM (object));

        mixer_stream = GVC_MIXER_STREAM (object);

        g_return_if_fail (mixer_stream->priv != NULL);

        g_free (mixer_stream->priv->name);
        mixer_stream->priv->name = NULL;

        g_free (mixer_stream->priv->icon_name);
        mixer_stream->priv->icon_name = NULL;

        G_OBJECT_CLASS (gvc_mixer_stream_parent_class)->finalize (object);
}