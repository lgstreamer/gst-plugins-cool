/* GStreamer command line playback testing utility
 *
 * Copyright (C) 2013-2014 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2013 Collabora Ltd.
 * Copyright (C) 2015 Centricular Ltd
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <locale.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <gst/cool/gstcool.h>

#include <gst/gst-i18n-app.h>
#include <gst/audio/audio.h>
#include <gst/pbutils/pbutils.h>
#include <gst/tag/tag.h>
#include <gst/math-compat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib/gprintf.h>

#include "gst-play-kb.h"

GST_DEBUG_CATEGORY (shvc_play_debug);
#define GST_CAT_DEFAULT shvc_play_debug

typedef struct
{
  GstElement *pipeline;

  GMainLoop *loop;
  guint bus_watch;
  guint timeout;

  GstState desired_state;       /* as per user interaction, PAUSED or PLAYING */

  GstElement *combiner;
  GstElement *video_decoder;
  GstElement *video_sink;
} GstPlay;

static gboolean quiet = FALSE;

static gboolean play_bus_msg (GstBus * bus, GstMessage * msg, gpointer data);
static gboolean play_timeout (gpointer user_data);
static void play_reset (GstPlay * play);

/* *INDENT-OFF* */
static void gst_play_printf (const gchar * format, ...) G_GNUC_PRINTF (1, 2);
/* *INDENT-ON* */

static void keyboard_cb (const gchar * key_input, gpointer user_data);

static void
gst_play_printf (const gchar * format, ...)
{
  gchar *str = NULL;
  va_list args;
  int len;

  if (quiet)
    return;

  va_start (args, format);

  len = g_vasprintf (&str, format, args);

  va_end (args);

  if (len > 0 && str != NULL)
    g_print ("%s", str);

  g_free (str);
}

#define g_print gst_play_printf

static GstPlay *
play_new (const gchar * bl_location, const gchar * el_location,
    gboolean dashmode)
{
  gchar *concat_pipeline;
  GstElement *pipeline;
  GstElement *bl_filesrc;
  GstElement *el_filesrc;
  GstPlay *play;
  gint video_port = -1;
  gint audio_port = -1;
  gint mixer_port = -1;
  gchar *core_type;
  GstStructure *resource;

  if (dashmode) {
    g_print ("Constructing dashmode ... \n");
    concat_pipeline =
        g_strdup_printf
        ("h265combiner name=hc ! openhevcdec name=videodecoder quality-layer-id=1 ! videoconvert ! queue ! xvimagesink name=videosink sync=False %s ! dashdemux ! qtdemux ! h265parse disable-passthrough=False ! hc.enhancement_sink %s ! dashdemux ! qtdemux ! h265parse disable-passthrough=False ! hc.base_sink",
        el_location, bl_location);
    pipeline = gst_parse_launch (concat_pipeline, NULL);
  } else {
    g_print ("Constructing filemode ... \n");
    concat_pipeline =
        "filesrc name=bl_filesrc ! qtdemux ! h265parse ! h265combiner name=hc ! openhevcdec name=videodecoder quality-layer-id=1 ! videoconvert ! autovideosink name=videosink sync=false filesrc name=el_filesrc ! qtdemux ! video/x-h265, stream-format=lhe1 ! h265parse ! hc.enhancement_sink";
    pipeline = gst_parse_launch (concat_pipeline, NULL);
  }

  if (pipeline == NULL) {
    g_print ("Could not launch pipeline. \n");
    return NULL;
  }

  play = g_new0 (GstPlay, 1);
  play->pipeline = pipeline;

  if (!dashmode) {
    bl_filesrc = gst_bin_get_by_name (GST_BIN (pipeline), "bl_filesrc");
    if (bl_filesrc == NULL) {
      g_print ("Could not find filesrc for BL. \n");
      return NULL;
    }
    g_print ("    BL location: %s \n", bl_location);
    g_object_set (bl_filesrc, "location", bl_location, NULL);
    gst_object_unref (bl_filesrc);

    el_filesrc = gst_bin_get_by_name (GST_BIN (pipeline), "el_filesrc");
    if (el_filesrc == NULL) {
      g_print ("Could not find filesrc for EL. \n");
      return NULL;
    }
    g_print ("    EL location: %s \n", el_location);
    g_object_set (el_filesrc, "location", el_location, NULL);
    gst_object_unref (el_filesrc);
  }

  play->combiner = gst_bin_get_by_name (GST_BIN (pipeline), "hc");
  if (play->combiner == NULL) {
    g_print ("Could not find combiner. \n");
    return NULL;
  }

  play->video_decoder =
      gst_bin_get_by_name (GST_BIN (pipeline), "videodecoder");
  if (play->video_decoder == NULL) {
    g_print ("Could not find videodecoder. \n");
    return NULL;
  }

  play->video_sink = gst_bin_get_by_name (GST_BIN (pipeline), "videosink");
  if (play->video_sink == NULL) {
    g_print ("Could not find videosink. \n");
    return NULL;
  }

  core_type = "HEVCP1_HEVC";
  video_port = 0;
  audio_port = -1;
  mixer_port = -1;

  resource = gst_structure_new ("resource-info",
      "core-type", G_TYPE_STRING, core_type,
      "video-port", G_TYPE_INT, video_port,
      "audio-port", G_TYPE_INT, audio_port,
      "mixer-port", G_TYPE_INT, mixer_port, NULL);

  g_object_set (play->video_decoder, "resource-info", resource, NULL);
  g_object_set (play->video_sink, "resource-info", resource, NULL);

  gst_structure_free (resource);

  play->loop = g_main_loop_new (NULL, FALSE);

  play->bus_watch = gst_bus_add_watch (GST_ELEMENT_BUS (play->pipeline),
      play_bus_msg, play);

  /* FIXME: make configurable incl. 0 for disable */
  play->timeout = g_timeout_add (100, play_timeout, play);

  play->desired_state = GST_STATE_PLAYING;

  return play;
}

static void
play_free (GstPlay * play)
{
  play_reset (play);

  gst_element_set_state (play->pipeline, GST_STATE_NULL);

  gst_object_unref (play->video_decoder);
  gst_object_unref (play->video_sink);
  gst_object_unref (play->combiner);
  gst_object_unref (play->pipeline);

  g_source_remove (play->bus_watch);
  g_source_remove (play->timeout);
  g_main_loop_unref (play->loop);

  g_free (play);
}

/* reset for new file/stream */
static void
play_reset (GstPlay * play)
{
}

static gboolean
play_bus_msg (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstPlay *play = user_data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ASYNC_DONE:

      /* dump graph on preroll */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (play->pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-play.async-done");

      g_print ("Prerolled.\r");
      break;
    case GST_MESSAGE_CLOCK_LOST:{
      g_print (_("Clock lost, selecting a new one\n"));
      gst_element_set_state (play->pipeline, GST_STATE_PAUSED);
      gst_element_set_state (play->pipeline, GST_STATE_PLAYING);
      break;
    }
    case GST_MESSAGE_LATENCY:
      g_print ("Redistribute latency...\n");
      gst_bin_recalculate_latency (GST_BIN (play->pipeline));
      break;
    case GST_MESSAGE_REQUEST_STATE:{
      GstState state;
      gchar *name;

      name = gst_object_get_path_string (GST_MESSAGE_SRC (msg));

      gst_message_parse_request_state (msg, &state);

      g_print ("Setting state to %s as requested by %s...\n",
          gst_element_state_get_name (state), name);

      gst_element_set_state (play->pipeline, state);
      g_free (name);
      break;
    }
    case GST_MESSAGE_EOS:
      /* print final position at end */
      play_timeout (play);
      g_print ("\n");
      /* and switch to next item in list */
      g_print ("%s\n", _("Reached end of stream."));
      g_main_loop_quit (play->loop);
      break;
    case GST_MESSAGE_WARNING:{
      GError *err;
      gchar *dbg = NULL;

      /* dump graph on warning */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (play->pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-play.warning");

      gst_message_parse_warning (msg, &err, &dbg);
      g_printerr ("WARNING %s\n", err->message);
      if (dbg != NULL)
        g_printerr ("WARNING debug information: %s\n", dbg);
      g_clear_error (&err);
      g_free (dbg);
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *dbg;

      /* dump graph on error */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (play->pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-play.error");

      gst_message_parse_error (msg, &err, &dbg);
      if (dbg != NULL)
        g_printerr ("ERROR debug information: %s\n", dbg);
      g_clear_error (&err);
      g_free (dbg);

      /* flush any other error messages from the bus and clean up */
      gst_element_set_state (play->pipeline, GST_STATE_NULL);

      /* try next item in list then */
      g_print ("%s\n", _("ERROR"));
      g_main_loop_quit (play->loop);
      break;
    }
    case GST_MESSAGE_PROPERTY_NOTIFY:{
      const GValue *val;
      const gchar *name;
      GstObject *obj;
      gchar *val_str = NULL;
      gchar *obj_name;

      gst_message_parse_property_notify (msg, &obj, &name, &val);

      obj_name = gst_object_get_path_string (GST_OBJECT (obj));
      if (val != NULL) {
        if (G_VALUE_HOLDS_STRING (val))
          val_str = g_value_dup_string (val);
        else if (G_VALUE_TYPE (val) == GST_TYPE_CAPS)
          val_str = gst_caps_to_string (g_value_get_boxed (val));
        else if (G_VALUE_TYPE (val) == GST_TYPE_TAG_LIST)
          val_str = gst_tag_list_to_string (g_value_get_boxed (val));
        else
          val_str = gst_value_serialize (val);
      } else {
        val_str = g_strdup ("(no value)");
      }

      gst_play_printf ("%s: %s = %s\n", obj_name, name, val_str);
      g_free (obj_name);
      g_free (val_str);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
play_timeout (gpointer user_data)
{
  GstPlay *play = user_data;
  gint64 pos = -1, dur = -1;
  const gchar *paused = _("Paused");
  gchar *status;

  gst_element_query_position (play->pipeline, GST_FORMAT_TIME, &pos);
  gst_element_query_duration (play->pipeline, GST_FORMAT_TIME, &dur);

  if (play->desired_state == GST_STATE_PAUSED) {
    status = (gchar *) paused;
  } else {
    gint len = g_utf8_strlen (paused, -1);
    status = g_newa (gchar, len + 1);
    memset (status, ' ', len);
    status[len] = '\0';
  }

  if (pos >= 0 && dur > 0) {
    gchar dstr[32], pstr[32];

    /* FIXME: pretty print in nicer format */
    g_snprintf (pstr, 32, "%" GST_TIME_FORMAT, GST_TIME_ARGS (pos));
    pstr[9] = '\0';
    g_snprintf (dstr, 32, "%" GST_TIME_FORMAT, GST_TIME_ARGS (dur));
    dstr[9] = '\0';
    g_print ("%s / %s %s\r", pstr, dstr, status);
  }

  return TRUE;
}

static void
do_play (GstPlay * play)
{
  if (play->desired_state != GST_STATE_PAUSED)
    gst_element_set_state (play->pipeline, play->desired_state);

  g_main_loop_run (play->loop);
}

static void
restore_terminal (void)
{
  gst_play_kb_set_key_handler (NULL, NULL);
}

static void
toggle_paused (GstPlay * play)
{
  if (play->desired_state == GST_STATE_PLAYING)
    play->desired_state = GST_STATE_PAUSED;
  else
    play->desired_state = GST_STATE_PLAYING;

  gst_element_set_state (play->pipeline, play->desired_state);
}

static void
print_keyboard_help (void)
{
  static struct
  {
    const gchar *key_desc;
    const gchar *key_help;
  } key_controls[] = {
    {
    N_("space"), N_("pause/unpause")}, {
    N_("q or ESC"), N_("quit")}, {
  "k", N_("show keyboard shortcuts")},};
  guint i, chars_to_pad, desc_len, max_desc_len = 0;

  g_print ("\n\n%s\n\n", _("Interactive mode - keyboard controls:"));

  for (i = 0; i < G_N_ELEMENTS (key_controls); ++i) {
    desc_len = g_utf8_strlen (key_controls[i].key_desc, -1);
    max_desc_len = MAX (max_desc_len, desc_len);
  }
  ++max_desc_len;

  for (i = 0; i < G_N_ELEMENTS (key_controls); ++i) {
    chars_to_pad = max_desc_len - g_utf8_strlen (key_controls[i].key_desc, -1);
    g_print ("\t%s", key_controls[i].key_desc);
    g_print ("%-*s: ", chars_to_pad, "");
    g_print ("%s\n", key_controls[i].key_help);
  }
  g_print ("\n");
}

static void
keyboard_cb (const gchar * key_input, gpointer user_data)
{
  GstPlay *play = (GstPlay *) user_data;
  gchar key = '\0';

  /* only want to switch/case on single char, not first char of string */
  if (key_input[0] != '\0' && key_input[1] == '\0')
    key = g_ascii_tolower (key_input[0]);

  switch (key) {
    case 'k':
      print_keyboard_help ();
      break;
    case ' ':
      toggle_paused (play);
      break;
    case 'q':
    case 'Q':
      g_main_loop_quit (play->loop);
      break;
    case 27:                   /* ESC */
      if (key_input[1] == '\0') {
        g_main_loop_quit (play->loop);
        break;
      }
    default:
      break;
  }
}

int
main (int argc, char **argv)
{
  GstPlay *play;
  gboolean print_version = FALSE;
  gboolean interactive = TRUE;
  gchar *bl_location;
  gchar *el_location;
  gboolean dashmode = FALSE;
  GError *err = NULL;
  GOptionContext *ctx;
  GOptionEntry options[] = {
    {"no-interactive", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE,
          &interactive,
        N_("Disable interactive control via the keyboard"), NULL},
    {"quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet,
        N_("Do not print any output (apart from errors)"), NULL},
    {"dashmode", 0, 0, G_OPTION_ARG_NONE, &dashmode,
        N_("Enable dashmode playback"), NULL},
    {"bl-location", 0, 0, G_OPTION_ARG_STRING, &bl_location,
        N_("BL location to use"), NULL},
    {"el-location", 0, 0, G_OPTION_ARG_STRING, &el_location,
        N_("EL location to use"), NULL},
    {NULL}
  };

  setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_set_prgname ("gst-shvc-play-" GST_API_VERSION);
  /* Ensure XInitThreads() is called if/when needed */
  g_setenv ("GST_GL_XINITTHREADS", "1", TRUE);

  ctx = g_option_context_new ("FILE1|URI1 [FILE2|URI2] [FILE3|URI3] ...");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    g_option_context_free (ctx);
    g_clear_error (&err);
    return 1;
  }
  g_option_context_free (ctx);

  gst_cool_init (&argc, &argv);

  GST_DEBUG_CATEGORY_INIT (shvc_play_debug, "play", 0, "gst-shvc-play");

  if (print_version) {
    gchar *version_str;

    version_str = gst_version_string ();
    g_print ("%s version %s\n", g_get_prgname (), PACKAGE_VERSION);
    g_print ("%s\n", version_str);
    g_print ("%s\n", GST_PACKAGE_ORIGIN);
    g_free (version_str);

    return 0;
  }

  /* prepare */
  g_print ("Preparing pipeline. \n");
  play = play_new (bl_location, el_location, dashmode);
  g_print ("Prepared pipeline. \n");
  if (play == NULL) {
    g_printerr
        ("Failed to create 'pipeline' element. Check your GStreamer installation.\n");
    return EXIT_FAILURE;
  }

  if (interactive) {
    if (gst_play_kb_set_key_handler (keyboard_cb, play)) {
      g_print (_("Press 'k' to see a list of keyboard shortcuts.\n"));
      atexit (restore_terminal);
    } else {
      g_print ("Interactive keyboard handling in terminal not available.\n");
    }
  }

  /* play */
  do_play (play);

  /* clean up */
  play_free (play);

  g_print ("\n");
  gst_deinit ();
  return 0;
}
