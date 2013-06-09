

#include <glib.h>
#include <gio/gio.h>
#include <string.h>

#include "gsr-fileutil.h"

void
gsr_set_audio_path (GSRWindowPrivate *priv)
{
  const gchar *path;
  // optinal read g_setting
  //GSettings *settings;

  //settings = g_settings_new ("org.gnome.Gsr");

  //g_settings_get (settings, "audio-path", "s", &priv->audio_path);

  /* get the video path from gsettings, xdg or hardcoded */
  if (!priv->audio_path || strcmp (priv->audio_path, "") == 0)
  {
    /* get xdg */
    /* this should not be freed */
    path = g_get_user_special_dir (G_USER_DIRECTORY_MUSIC);
    /* this should be freed */
    /* TODO: we should check if folder exist */
    priv->audio_path = g_build_filename (path, "Records", NULL);
    if (strcmp (priv->audio_path, "") == 0)
    {
      g_print("still no path!!!\n");
    }
  }
}

/* generare filename based on time stamp.
 *
 */
void
gsr_filename_from_datetime (GSRWindowPrivate *priv)
{
  g_free (priv->basename);
  /* we support only ogg. No need to bother about different file extensions */
  priv->basename = g_date_time_format (priv->datetime, "%Y-%m-%d-%H%M%S.ogg");

  g_free (priv->record_filename);
  priv->record_filename = g_build_filename (priv->audio_path,
      priv->basename, NULL);
}
