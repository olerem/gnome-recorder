/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <libintl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "cc-sound-panel.h"
#include "gvc-mixer-dialog.h"
#include "gvc-log.h"

G_DEFINE_DYNAMIC_TYPE (CcSoundPanel, cc_sound_panel, CC_TYPE_PANEL)

static void cc_sound_panel_finalize (GObject *object);

static void
cc_sound_panel_class_init (CcSoundPanelClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = cc_sound_panel_finalize;
}

static void
cc_sound_panel_class_finalize (CcSoundPanelClass *klass)
{
}

static void
cc_sound_panel_finalize (GObject *object)
{
        CcSoundPanel *panel = CC_SOUND_PANEL (object);

        if (panel->dialog != NULL)
                panel->dialog = NULL;
        if (panel->connecting_label != NULL)
                panel->connecting_label = NULL;
        if (panel->control != NULL) {
                g_object_unref (panel->control);
                panel->control = NULL;
        }

        G_OBJECT_CLASS (cc_sound_panel_parent_class)->finalize (object);
}

static void
on_control_ready (GvcMixerControl *control,
                  CcSoundPanel    *panel)
{
        if (panel->dialog != NULL)
                return;

        if (panel->connecting_label) {
                gtk_widget_destroy (panel->connecting_label);
                panel->connecting_label = NULL;
        }

        panel->dialog = gvc_mixer_dialog_new (control);
        gtk_container_add (GTK_CONTAINER (panel),
                           GTK_WIDGET (panel->dialog));
        gtk_widget_show (GTK_WIDGET (panel->dialog));
}

static void
cc_sound_panel_init (CcSoundPanel *self)
{
        gvc_log_init ();
        gvc_log_set_debug (TRUE);

        gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                           ICON_DATA_DIR);
        gtk_window_set_default_icon_name ("multimedia-volume-control");

        self->control = gvc_mixer_control_new ("GNOME Volume Control Dialog");
        g_signal_connect (self->control,
                          "ready",
                          G_CALLBACK (on_control_ready),
                          self);
        gvc_mixer_control_open (self->control);

        self->connecting_label = gtk_label_new (_("Waiting for sound system to respond"));
        gtk_container_add (GTK_CONTAINER (self), self->connecting_label);
        gtk_widget_show (self->connecting_label);
}

void
cc_sound_panel_register (GIOModule *module)
{
        cc_sound_panel_register_type (G_TYPE_MODULE (module));
        g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                        CC_TYPE_SOUND_PANEL,
                                        "sound", 0);
}

/* GIO extension stuff */
void
g_io_module_load (GIOModule *module)
{
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

        /* register the panel */
        cc_sound_panel_register (module);
}

void
g_io_module_unload (GIOModule *module)
{
}

