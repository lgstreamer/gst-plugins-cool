/* GStreamer Plugins Cool
 * Copyright (C) 2014 LG Electronics, Inc.
 *	Author : Jeongseok Kim <jeongseok.kim@lge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcool.h"

#include <stdlib.h>

static gboolean gst_cool_initialized = FALSE;

static void gst_cool_init_config (void);
static void gst_cool_load_configuration (void);
static void gst_cool_adjust_sw_decoder (void);
static void gst_cool_adjust_sw_video_decoder (void);
static void gst_cool_load_debug_configuration (void);
static gboolean parse_option_arg (const gchar * s_opt, const gchar * arg);

enum
{
  ARG_USE_SW_DECODER = 1
};

enum
{
  GST_COOL_RANK_PRIMARY = 300
};

static gboolean use_sw_decoder = FALSE;

static GKeyFile *config = NULL;
GKeyFile *
gst_cool_get_configuration (void)
{
  return config;
}

gboolean
gst_cool_init_check (int *argc, char **argv[], GError ** err)
{
  gboolean res;
  gint i;

  // TODO: configuration should be loaded by given path
  // static const gchar *config_name[] = { "gstcool.conf", NULL };
  // static const gchar *env_config_name[] = { "GST_COOL_CONFIG_DIR", NULL };

  if (gst_cool_initialized) {
    GST_DEBUG ("already initialized gst-cool");
    return TRUE;
  }

  if (argc && argv) {
    for (i = 0; i < *argc; i++) {
      if ((*argv)[i][0] == '-' && (*argv)[i][1] == '-') {
        gchar **split;
        gint split_length;
        split = g_strsplit ((*argv)[i], "=", -1);
        split_length = g_strv_length (split);
        if (split_length == 2) {
          parse_option_arg (split[0], split[1]);
        }
        g_strfreev (split);
      }
    }
  }

  gst_cool_init_config ();

  /* set debug config before gst_init */
  gst_cool_load_debug_configuration ();

#ifdef GST_COOL_ENABLE_OPTION_PARSING
  res = TRUE;
#else
  res = gst_init_check (argc, argv, err);
#endif

  if (!res) {
    return FALSE;
  }

  gst_cool_load_configuration ();

  /* adjust s/w audio decoder */
  gst_cool_adjust_sw_decoder ();
  gst_cool_adjust_sw_video_decoder ();

  gst_cool_initialized = res;

  return res;
}

void
gst_cool_init (int *argc, char **argv[])
{
  GError *err = NULL;

  if (!gst_cool_init_check (argc, argv, &err)) {
    g_print ("Failed to initialize GStreamer Cool: %s\n",
        err ? err->message : "Unknown");
    if (err)
      g_error_free (err);
    exit (1);
  }
}

static void
gst_cool_init_config (void)
{
  GError *err = NULL;
  const gchar *cool_config_file;

  if ((cool_config_file = g_getenv ("GST_COOL_CONFIG")) != NULL) {
    GST_DEBUG ("loading gst-cool config from GST_COOL_CONFIG: %s",
        cool_config_file);
  } else {
    // FIXME: the path should come from configuration
    cool_config_file = "/etc/gst/gstcool.conf";
  }

  config = g_key_file_new ();

  if (!g_key_file_load_from_file (config, cool_config_file, G_KEY_FILE_NONE,
          &err)) {
    GST_ERROR ("Failed to load gst-cool configuration file(%s): %s",
        cool_config_file, err->message);
    g_error_free (err);
  }
}

static void
gst_cool_load_configuration (void)
{
  gchar **sections = NULL;
  gsize n_sections;

  gchar **rank_items = NULL;
  gsize n_rank_items;

  gint i;
  GError *err = NULL;

  sections = g_key_file_get_groups (config, &n_sections);

  // TODO: I don't know which sections are required for the future release.
  for (i = 0; i < n_sections; i++) {

    if (g_strcmp0 ("rank", sections[i]) != 0) {
      GST_DEBUG ("ignored %s section", sections[i]);
      continue;
    }
  }

  err = NULL;
  if (!(rank_items = g_key_file_get_keys (config, "rank", &n_rank_items, &err))) {
    GST_ERROR ("Unable to read plugin names for setting rank '%s'",
        err->message);
    g_error_free (err);
    goto done;
  }

  for (i = 0; i < n_rank_items; i++) {
    gint rank;
    err = NULL;
    rank = g_key_file_get_integer (config, "rank", rank_items[i], &err);

    if (err) {
      GST_ERROR ("Unable to read rank value for %s: %s", rank_items[i],
          err->message);
      g_error_free (err);
      continue;
    }

    gst_cool_set_rank (rank_items[i], rank);
  }

done:
  g_strfreev (rank_items);
  g_strfreev (sections);
}

static void
gst_cool_adjust_sw_decoder (void)
{
  gchar **sections = NULL;
  gsize n_sections;
  gchar **decoder_items = NULL;
  gsize n_decoder_items;
  gint i;
  GError *err = NULL;
  gint rank;

  sections = g_key_file_get_groups (config, &n_sections);

  // TODO: I don't know which sections are required for the future release.
  for (i = 0; i < n_sections; i++) {
    if (g_strcmp0 ("sw_decoder", sections[i]) != 0) {
      GST_DEBUG ("ignored %s section", sections[i]);
      continue;
    }
  }

  err = NULL;
  if (!(decoder_items =
          g_key_file_get_keys (config, "sw_decoder", &n_decoder_items, &err))) {
    GST_ERROR ("Unable to read plugin names '%s'", err->message);
    g_error_free (err);
    goto done;
  }

  if (!g_key_file_has_group (config, "rank")) {
    GST_ERROR ("Skip rank adjustment. User rank group does not exist.");
    goto done;
  }

  rank = g_key_file_get_integer (config, "rank", "decproxy", &err) - 1;
  if (err) {
    GST_ERROR ("Unable to read rank value for decproxy: %s", err->message);
    g_error_free (err);
    rank = GST_COOL_RANK_PRIMARY;
  }

  for (i = 0; i < n_decoder_items; i++) {
    if (!use_sw_decoder) {
      err = NULL;
      rank =
          g_key_file_get_integer (config, "sw_decoder", decoder_items[i], &err);

      if (err) {
        GST_ERROR ("Unable to read rank value for %s: %s", decoder_items[i],
            err->message);
        g_error_free (err);
        continue;
      }
    }

    gst_cool_set_rank (decoder_items[i], rank);
  }

done:
  g_strfreev (decoder_items);
  g_strfreev (sections);
}

static void
gst_cool_adjust_sw_video_decoder (void)
{
  gchar **decoder_items = NULL;
  gsize n_decoder_items;
  GError *err = NULL;
  gint rank, i;

  if (!(decoder_items = g_key_file_get_keys (config, "sw_video_decoder",
              &n_decoder_items, &err))) {
    GST_ERROR ("Unable to read plugin names '%s'", err->message);
    g_error_free (err);
    goto done;
  }

  for (i = 0; i < n_decoder_items; i++) {
    rank = g_key_file_get_integer (config, "sw_video_decoder", decoder_items[i],
        &err);

    if (err) {
      GST_ERROR ("Unable to read rank value for %s: %s", decoder_items[i],
          err->message);
      g_error_free (err);
      err = NULL;
      continue;
    }

    gst_cool_set_rank (decoder_items[i], rank);
    GST_DEBUG ("set sw video decoder %s rank to %d", decoder_items[i], rank);
  }

done:
  g_strfreev (decoder_items);
}

void
gst_cool_give_priority_sw_decoder (gboolean use_sw_dec)
{
  if (use_sw_decoder == use_sw_dec) {
    GST_DEBUG ("Ignored. Priority of decoders has already been adjusted.");
    return;
  }

  use_sw_decoder = use_sw_dec;
  GST_DEBUG ("Give priority to sw decoder: %s",
      use_sw_decoder ? "TRUE" : "FALSE");
  gst_cool_adjust_sw_decoder ();
}

static void
gst_cool_load_debug_configuration (void)
{
  gchar *debug_mode = NULL;
  gchar *gst_debug = NULL;
  gchar *gst_debug_file = NULL;
  gchar *gst_debug_file_max_count = NULL;
  gchar *gst_debug_dump_dot_dir = NULL;
  gint gst_debug_no_color = 0;
  gchar *gst_tracers = NULL;
  gchar *gst_debug_file_overwrite = NULL;
  gchar *pmlog_klass = NULL;
  gchar *pmlog_element = NULL;
  gchar *pmlog_event = NULL;

  GError *err = NULL;

  gst_tracers = g_key_file_get_string (config, "debug", "GST_TRACERS", &err);
  if (err) {
    GST_WARNING ("Unable to read GST_TRACERS option: %s", err->message);
    g_error_free (err);
    err = NULL;
  }

  pmlog_klass =
      g_key_file_get_string (config, "debug", "PMLOG_DEBUG_KLASS", &err);
  if (err) {
    GST_WARNING ("Unable to read PMLOG_DEBUG_KLASS option: %s", err->message);
    g_error_free (err);
    err = NULL;
  }

  pmlog_element =
      g_key_file_get_string (config, "debug", "PMLOG_DEBUG_ELEMENT", &err);
  if (err) {
    GST_WARNING ("Unable to read PMLOG_DEBUG_ELEMENT option: %s", err->message);
    g_error_free (err);
    err = NULL;
  }

  pmlog_event =
      g_key_file_get_string (config, "debug", "PMLOG_DEBUG_EVENT", &err);
  if (err) {
    GST_WARNING ("Unable to read PMLOG_DEBUG_EVENT option: %s", err->message);
    g_error_free (err);
    err = NULL;
  }


  debug_mode = g_key_file_get_string (config, "debug", "DEBUG_MODE", &err);
  if (err) {
    GST_WARNING ("Unable to read debug mode: %s", err->message);
    g_error_free (err);
    err = NULL;
  }

  /* if debug mode is not enable, do not set debug environment variable */
  if (g_strcmp0 (debug_mode, "enable") == 0) {

    gst_debug = g_key_file_get_string (config, "debug", "GST_DEBUG", &err);
    if (err) {
      GST_WARNING ("Unable to read GST_DEBUG option: %s", err->message);
      g_error_free (err);
      err = NULL;
    }

    gst_debug_file =
        g_key_file_get_string (config, "debug", "GST_DEBUG_FILE", &err);
    if (err) {
      GST_WARNING ("Unable to read GST_DEBUG_FILE option: %s", err->message);
      g_error_free (err);
      err = NULL;
    }

    gst_debug_file_max_count =
        g_key_file_get_string (config, "debug", "GST_DEBUG_FILE_MAX_COUNT",
        &err);
    if (err) {
      GST_WARNING ("Unable to read GST_DEBUG_FILE_MAX_COUNT option: %s",
          err->message);
      g_error_free (err);
      err = NULL;
    }

    gst_debug_dump_dot_dir = g_key_file_get_string (config, "debug",
        "GST_DEBUG_DUMP_DOT_DIR", &err);
    if (err) {
      GST_WARNING ("Unable to read GST_DEBUG_DUMP_DOT_DIR option: %s",
          err->message);
      g_error_free (err);
      err = NULL;
    }

    gst_debug_no_color = g_key_file_get_integer (config, "debug",
        "GST_DEBUG_NO_COLOR", &err);
    if (err) {
      GST_WARNING ("Unable to read GST_DEBUG_NO_COLOR option: %s",
          err->message);
      g_error_free (err);
      err = NULL;
    }

    gst_debug_file_overwrite =
        g_key_file_get_string (config, "debug", "GST_DEBUG_FILE_OVERWRITE",
        &err);
    if (err) {
      GST_WARNING ("Unable to read GST_DEBUG_FILE_OVERWRITE option: %s",
          err->message);
      g_error_free (err);
      err = NULL;
    }
  }

  GST_WARNING ("debug conf: DEBUG_MODE=%s, GST_DEBUG=%s, GST_DEBUG_FILE=%s\n"
      "GST_DEBUG_DUMP_DOT_DIR=%s, GST_DEBUG_GST_DEBUG_NO_COLOR=%d"
      "GST_TRACERS=%s, GST_DEBUG_FILE_OVERWRITE=%s",
      debug_mode,
      gst_debug, gst_debug_file, gst_debug_dump_dot_dir, gst_debug_no_color,
      gst_tracers, gst_debug_file_overwrite);

  if (gst_debug)
    g_setenv ("GST_DEBUG", gst_debug, 1);
  if (gst_debug_file)
    g_setenv ("GST_DEBUG_FILE", gst_debug_file, 1);
  if (gst_debug_file_max_count)
    g_setenv ("GST_DEBUG_FILE_MAX_COUNT", gst_debug_file_max_count, 1);
  if (gst_debug_dump_dot_dir)
    g_setenv ("GST_DEBUG_DUMP_DOT_DIR", gst_debug_dump_dot_dir, 1);
  if (gst_debug_no_color)
    g_setenv ("GST_DEBUG_NO_COLOR", "1", 1);
  if (gst_tracers && (gst_tracers[0] != 0))
    g_setenv ("GST_TRACERS", gst_tracers, 1);
  if (gst_debug_file_overwrite)
    g_setenv ("GST_DEBUG_FILE_OVERWRITE", gst_debug_file_overwrite, 1);
  if (pmlog_klass)
    g_setenv ("PMLOG_DEBUG_KLASS", pmlog_klass, 1);
  if (pmlog_element)
    g_setenv ("PMLOG_DEBUG_ELEMENT", pmlog_element, 1);
  if (pmlog_event)
    g_setenv ("PMLOG_DEBUG_EVENT", pmlog_event, 1);

  g_free (debug_mode);
  g_free (gst_debug);
  g_free (gst_debug_file);
  g_free (gst_debug_file_max_count);
  g_free (gst_debug_dump_dot_dir);
  g_free (gst_tracers);
  g_free (gst_debug_file_overwrite);
  g_free (pmlog_klass);
  g_free (pmlog_element);
  g_free (pmlog_event);
}

void
gst_cool_set_rank (const gchar * plugin, gint rank)
{
  GstPluginFeature *feature;
  feature =
      gst_registry_find_feature (gst_registry_get (), plugin,
      GST_TYPE_ELEMENT_FACTORY);

  if (!feature) {
    GST_INFO ("Unable to set rank of '%s' as %d", plugin, rank);
    return;
  }

  gst_plugin_feature_set_rank (feature, rank);

  GST_DEBUG ("'%s' has %d rank value", plugin, rank);

  gst_object_unref (feature);
}

void
gst_cool_set_decode_buffer_size (guint in_size, guint out_size)
{
  GKeyFile *config = gst_cool_get_configuration ();

  g_key_file_set_integer (config, "decode", "in_size", in_size);

  g_key_file_set_integer (config, "decode", "out_size", out_size);

  GST_DEBUG ("decode buffer has changed in: %d, out: %d", in_size, out_size);
}

gchar *
gst_cool_get_external_video_decoder ()
{
  gchar *fac_name = NULL;
  GError *err = NULL;
  GKeyFile *config = gst_cool_get_configuration ();
  if (!config)
    return NULL;
  if (!(fac_name =
          g_key_file_get_string (config, "external_decoder", "video", &err))) {
    GST_ERROR ("Unable to read plugin names '%s'", err->message);
    g_error_free (err);
    return NULL;
  }
  return fac_name;
}

gchar *
gst_cool_get_external_video_sink ()
{
  gchar *fac_name = NULL;
  GError *err = NULL;
  GKeyFile *config = gst_cool_get_configuration ();
  if (!config)
    return NULL;
  if (!(fac_name =
          g_key_file_get_string (config, "external_sink", "video", &err))) {
    GST_ERROR ("Unable to read plugin names '%s'", err->message);
    g_error_free (err);
    return NULL;
  }
  return fac_name;
}

static gboolean
parse_one_option (gint opt, const gchar * arg)
{
  switch (opt) {
    case ARG_USE_SW_DECODER:{
      if ((g_strcmp0 (arg, "TRUE") == 0) || (g_strcmp0 (arg, "1") == 0))
        use_sw_decoder = TRUE;
      else
        use_sw_decoder = FALSE;
      break;
    }
    default:
      // Unknown option
      return FALSE;
  }

  return TRUE;
}

static gboolean
parse_option_arg (const gchar * opt, const gchar * arg)
{
  static const struct
  {
    const gchar *opt;
    int val;
  } options[] = {
    {
        "--use-sw-decoder", ARG_USE_SW_DECODER}, {
        NULL}
  };
  gint val = 0, n;

  for (n = 0; options[n].opt; n++) {
    if (!g_strcmp0 (opt, options[n].opt)) {
      val = options[n].val;
      break;
    }
  }

  return parse_one_option (val, arg);
}
