

#include <glib.h>
#include <gio/gio.h>
#include <string.h>

#include "gsr-fileutil.h"


/* generare filename based on time stamp.
 *
 */

gchar*
gsr_generate_filename (GSRWindow *window)
{
  gchar *string;
  GDateTime *datetime;

  datetime = g_date_time_new_now_local ();
  g_assert (datetime != NULL);

  if (window) {
    if (window->priv->datetime)
      g_date_time_unref (window->priv->datetime);

    window->priv->datetime = g_date_time_ref (datetime);
  }

  string = g_date_time_format (datetime, "%Y-%m-%d-%H%M%S");
  g_date_time_unref (datetime);

  return string;
}

#if 0
gsr_get_folder_path ()
{
  // optinal read g_setting
  //GSettings *settings;
  gcahr *path;

  //settings = g_settings_new ("org.gnome.Gsr");

  //g_settings_get (settings, "audio-path", "s", &priv->audio_path);

  /* get the video path from gsettings, xdg or hardcoded */
  if (!priv->audio_path || strcmp (priv->audio_path, "") == 0)
  {
    /* get xdg */
    /* this should not be freed */
    path = g_get_user_special_dir (G_USER_DIRECTORY_MUSIC);
    /* this should be freed */
    priv->audio_path = g_build_filename (path, "Records", NULL);
    if (strcmp (priv->audio_path, "") == 0)
    {
      gprint("still no path!!!\n");
    }
  }
}
#endif
