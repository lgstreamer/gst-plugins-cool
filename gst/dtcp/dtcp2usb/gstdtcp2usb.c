/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 LG Electronics, Inc. <support@lge.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:element-dtcp2usb
 *
 * Handles DTCP2 negotiation over usb and content encryption.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! dtcp2usb ! fakesink drmtype=""
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>

#include "gstdtcp2usb.h"
#include "dtcp2_stack.h"
#include <mediadrm/svp/svb_decryptor.h>

GST_DEBUG_CATEGORY_STATIC (gst_dtcp2usb_debug);
#define GST_CAT_DEFAULT gst_dtcp2usb_debug

enum
{
  PROP_0,
  PROP_SIGNAL,
  PROP_SIGNAL_IN,
  PROP_SIGNAL_OUT,
  PROP_DRM_TYPE
};

struct _GstDtcp2UsbStack
{
  GThread *thread;
  GMutex mutex;
  sem_t sem;

  gboolean stop_running;
  gboolean notification_sent;   /* This is for the convenience of the stack thread.
                                 * It does not need to obtain the lock to know has
                                 * the data plane already been signalled. (This
                                 * variable is only accessed by the stack thread.)
                                 */
  gboolean ready_to_process;    /* This is for the convenience of the data plane.
                                 * It does not need to obrain the lock to know has
                                 * the dataplane already been signalled to be read
                                 * to process the data.
                                 */
  gboolean authenticated;
  int dev_in;
  int dev_out;
};

#define DEFAULT_DTCP_SIGNAL_DEVICE "/dev/dtcp"

#define DTCP_STACK_INSTANCE (1)
#define DTCP_STREAM_INSTANCE (128)

#define DTCP_STACK_KEEP_RUNNING (0)
#define DTCP_STACK_STOP_RUNNING (1)

#define DTCP_BLOCK_LENGTH (16)

/* Cumulative size of the CMI packet + the PCP header */
#define DTCP_HEADER_SIZE (64)

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_dtcp2usb_parent_class parent_class
G_DEFINE_TYPE (GstDtcp2Usb, gst_dtcp2usb, GST_TYPE_ELEMENT);

static void gst_dtcp2usb_finalize (GObject * object);

static void gst_dtcp2usb_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dtcp2usb_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_dtcp2usb_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_dtcp2usb_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_dtcp2usb_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static gboolean gst_dtcp2usb_launch_stream (GstDtcp2Usb * filter);
static void gst_dtcp2usb_teardown_stream (GstDtcp2Usb * filter);
static GstBuffer *gst_dtcp2usb_encrypt_data (GstDtcp2Usb * filter,
    GstBuffer * input_buf);
static void gst_dtcp2usb_send_upstream_event (GstDtcp2Usb * filter);

static void
gst_dtcp2usb_class_init (GstDtcp2UsbClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_dtcp2usb_finalize;

  gobject_class->set_property = gst_dtcp2usb_set_property;
  gobject_class->get_property = gst_dtcp2usb_get_property;

  gstelement_class->change_state = gst_dtcp2usb_change_state;

  g_object_class_install_property (gobject_class, PROP_SIGNAL,
      g_param_spec_string ("signal", "DTCP signal path",
          "The local filesystem path of DTCP signal plane device to open",
          DEFAULT_DTCP_SIGNAL_DEVICE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SIGNAL_IN,
      g_param_spec_string ("signal-in", "Incoming DTCP signal path",
          "The local filesystem path of the incoming DTCP signal plane device. This allows separating incoming and outgoing signal streams",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SIGNAL_OUT,
      g_param_spec_string ("signal-out", "Outgoing DTCP signal path",
          "The local filesystem path of the outgoing DTCP signal plane device. This allows separating incoming and outgoing signal streams",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DRM_TYPE,
      g_param_spec_string ("drmtype", "DRM Type",
          "DRM type for DRM decryptor selection", NULL, G_PARAM_READWRITE));

  gst_element_class_set_static_metadata (gstelement_class,
      "DTCP2 USB source plugin",
      "Codec/Parser/Converter/Video",
      "Handles DTCP2 negotiation over USB and content encryption.",
      "(c) LG Electronics Inc. 2020, <support@lge.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_dtcp2usb_init (GstDtcp2Usb * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dtcp2usb_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dtcp2usb_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->stack = NULL;
  filter->signal = NULL;
  filter->signal_in = NULL;
  filter->signal_out = NULL;
  filter->drm_type = NULL;
  filter->svb_decryptor = NULL;
  filter->inactive_stream_id = NULL;
  filter->active_stream_id = NULL;
  filter->stream_changed = FALSE;
}

static void
gst_dtcp2usb_finalize (GObject * object)
{
  GstDtcp2Usb *filter = GST_DTCP2USB (object);

  g_clear_pointer (&filter->stack, g_free);
  g_clear_pointer (&filter->signal, g_free);
  g_clear_pointer (&filter->signal_in, g_free);
  g_clear_pointer (&filter->signal_out, g_free);
  g_clear_pointer (&filter->drm_type, g_free);
  g_clear_pointer (&filter->inactive_stream_id, g_free);
  g_clear_pointer (&filter->active_stream_id, g_free);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dtcp2usb_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDtcp2Usb *filter = GST_DTCP2USB (object);

  switch (prop_id) {
    case PROP_SIGNAL:
      g_free (filter->signal);
      filter->signal = g_value_dup_string (value);
      GST_INFO_OBJECT (filter, "signal: %s", GST_STR_NULL (filter->signal));
      break;
    case PROP_SIGNAL_IN:
      g_free (filter->signal_in);
      filter->signal_in = g_value_dup_string (value);
      GST_INFO_OBJECT (filter, "signal-in: %s",
          GST_STR_NULL (filter->signal_in));
      break;
    case PROP_SIGNAL_OUT:
      g_free (filter->signal_out);
      filter->signal_out = g_value_dup_string (value);
      GST_INFO_OBJECT (filter, "signal-out: %s",
          GST_STR_NULL (filter->signal_out));
      break;
    case PROP_DRM_TYPE:
      g_free (filter->drm_type);
      filter->drm_type = g_value_dup_string (value);
      GST_INFO_OBJECT (filter, "drm-type: %s", GST_STR_NULL (filter->drm_type));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dtcp2usb_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDtcp2Usb *filter = GST_DTCP2USB (object);

  switch (prop_id) {
    case PROP_SIGNAL:
      g_value_set_string (value, filter->signal);
      break;
    case PROP_SIGNAL_IN:
      g_value_set_string (value, filter->signal_in);
      break;
    case PROP_SIGNAL_OUT:
      g_value_set_string (value, filter->signal_out);
      break;
    case PROP_DRM_TYPE:
      g_value_set_string (value, filter->drm_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_dtcp2usb_change_state (GstElement * element, GstStateChange transition)
{
  GstDtcp2Usb *filter;
  int status;

  filter = GST_DTCP2USB (element);
  GST_INFO_OBJECT (filter, "GST_STATE_CHANGE_%s_TO_%s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      status = DTCP_Stack_Init ();
      if (status != 0) {
        GST_ERROR_OBJECT (filter, "DTCP_Stack_Init failed: %d", status);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_dtcp2usb_launch_stream (filter)) {
        GST_ERROR_OBJECT (filter, "Failed to setup dtcp stream");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_dtcp2usb_teardown_stream (filter);
      gst_dtcp2usb_send_upstream_event (filter);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      DTCP_Stack_Done ();
      DestroySvbDecryptor (filter->svb_decryptor);
      filter->svb_decryptor = NULL;
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
gst_dtcp2usb_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDtcp2Usb *filter;

  filter = GST_DTCP2USB (parent);

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
    {
      const gchar *stream_id;

      gst_event_parse_stream_start (event, &stream_id);
      if (filter->active_stream_id &&
          !g_str_equal (filter->active_stream_id, stream_id)) {
        GST_INFO_OBJECT (filter, "Stream changed from %s to %s",
            filter->active_stream_id, stream_id);
        g_free (filter->inactive_stream_id);
        filter->inactive_stream_id = g_strdup (filter->active_stream_id);
        filter->stream_changed = TRUE;
      }
      g_free (filter->active_stream_id);
      filter->active_stream_id = g_strdup (stream_id);
      break;
    }
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstStructure *s;
      const gchar *drm_type;

      gst_event_parse_caps (event, &caps);
      s = gst_caps_get_structure (caps, 0);

      drm_type = gst_structure_get_string (s, "drm-type");
      if (drm_type) {
        g_free (filter->drm_type);
        filter->drm_type = g_strdup (drm_type);
        GST_INFO_OBJECT (filter, "drm-type: %s",
            GST_STR_NULL (filter->drm_type));
      }
      break;
    }
    default:
      break;
  }
  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_dtcp2usb_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstDtcp2Usb *filter;
  GstBuffer *output;
  gboolean ret;

  filter = GST_DTCP2USB (parent);

  output = gst_dtcp2usb_encrypt_data (filter, buf);
  ret = output ? gst_pad_push (filter->srcpad, output) : GST_FLOW_ERROR;
  gst_buffer_unref (buf);

  return ret;
}

static int
gst_dtcp2usb_wait_fd_readable (GstDtcp2Usb * filter, int fd, int timeout_ms)
{
  int n;
  fd_set rfds;
  struct timeval tv;

  FD_ZERO (&rfds);
  FD_SET (fd, &rfds);

  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = 1000 * (timeout_ms % 1000);

  n = select (fd + 1, &rfds, NULL, NULL, &tv);
  GST_DEBUG_OBJECT (filter, "select() returned %d", n);

  if (n == 0) {
    return 0;                   /* 0 = timeout */
  } else if (n < 0) {
    GST_WARNING_OBJECT (filter, "select() %s", strerror (errno));
    return -1;                  /* <0 = error */
  }

  if (FD_ISSET (fd, &rfds)) {
    /* We have input and data is available. */
    return 1;
  }

  /* exception? */
  GST_WARNING_OBJECT (filter, "Unexpected situation");
  return -1;
}

static int
gst_dtcp2usb_outgoing_usb_traffic (void *traffic_context,
    uint8_t stack_instance, const uint8_t * data_bytes_out, size_t byte_count)
{
  GstDtcp2Usb *filter;
  GstDtcp2UsbStack *stack;
  ssize_t bytes;

  (void) stack_instance;
  filter = (GstDtcp2Usb *) traffic_context;
  g_assert (filter != NULL);

  stack = filter->stack;
  if (!stack || stack->dev_out < 0) {
    GST_WARNING_OBJECT (filter, "USB write failed, device not opened");
    return -1;
  }

  bytes = write (stack->dev_out, data_bytes_out, byte_count);
  return bytes;
}

static int
gst_dtcp2usb_incoming_usb_traffic (void *traffic_context,
    uint8_t stack_instance,
    uint8_t * data_bytes_in, size_t byte_count, uint32_t timeout_ms)
{
  GstDtcp2Usb *filter;
  GstDtcp2UsbStack *stack;
  int status;
  ssize_t bytes;

  (void) stack_instance;
  filter = (GstDtcp2Usb *) traffic_context;
  g_assert (filter != NULL);

  stack = filter->stack;
  if (!stack || stack->dev_in < 0) {
    GST_WARNING_OBJECT (filter, "USB read failed, device not opened");
    return -1;
  }

  status = gst_dtcp2usb_wait_fd_readable (filter, stack->dev_in, timeout_ms);
  if (status <= 0)
    return status;

  bytes = read (stack->dev_in, data_bytes_in, byte_count);
  return bytes;
}

static int
gst_dtcp2usb_check_notification (GstDtcp2Usb * filter)
{
  GstDtcp2UsbStack *stack;
  int ret = DTCP_STACK_KEEP_RUNNING;

  g_assert (filter != NULL);
  stack = filter->stack;
  g_assert (stack != NULL);

  if (!stack->notification_sent && stack->authenticated) {
    if (0 != sem_post (&stack->sem)) {
      GST_ERROR_OBJECT (filter,
          "DTCP worked thread failed to signalled the semaphore: %s",
          strerror (errno));
      ret = DTCP_STACK_STOP_RUNNING;    /* Stops the DTCP stack. */
    } else {
      GST_INFO_OBJECT (filter, "DTCP worked thread signalled the semaphore");
      stack->notification_sent = TRUE;
    }
  }

  return ret;
}

static int
gst_dtcp2usb_stack_callback (void *cb_param,
    uint8_t stack_instance,
    DTCP_Stack_Status_t status, DTCP_Stack_InfoBlock_t * const info_block)
{
  GstDtcp2Usb *filter;
  GstDtcp2UsbStack *stack;
  char *stack_status;
  int ret = DTCP_STACK_KEEP_RUNNING;

  filter = (GstDtcp2Usb *) cb_param;
  g_assert (filter != NULL);
  stack = filter->stack;
  g_assert (stack != NULL);

  switch (status) {
    case DTCP_STACK_DISCONNECTED:
      stack_status = "DISCONNECTED";
      break;
    case DTCP_STACK_CONNECTED:
      stack_status = "CONNECTED";
      break;
    case DTCP_STACK_AUTHENTICATED:
      stack_status = "AUTHENTICATED";
      stack->authenticated = TRUE;
      ret = DTCP_Stack_StreamCreate (DTCP_STREAM_INSTANCE, DTCP_STACK_INSTANCE);
      if (ret != 0) {
        GST_ERROR_OBJECT (filter, "DTCP_Stack_StreamCreate failed: %d", ret);
        ret = DTCP_STACK_STOP_RUNNING;  /* Stops the DTCP stack. */
      } else {
        GST_INFO_OBJECT (filter, "DTCP stream created");
        ret = gst_dtcp2usb_check_notification (filter);
      }
      break;
    case DTCP_STACK_ALIVE:
      stack_status = "ALIVE";
      ret = gst_dtcp2usb_check_notification (filter);
      break;
    case DTCP_STACK_ERROR:
      stack_status = "ERROR";
      ret = DTCP_STACK_STOP_RUNNING;
      break;
    case DTCP_STACK_KEY_EXPIRED:
      stack_status = "KEY_EXPIRED";
      break;
    case DTCP_STACK_PRE_KXGEN:
      stack_status = "PRE_KXGEN";
      break;
    case DTCP_STACK_PROTOCOL_ERROR:
      stack_status = "PROTOCOL_ERROR";
      ret = DTCP_STACK_STOP_RUNNING;
      break;
    case DTCP_STACK_TIMEOUT:
      stack_status = "TIMEOUT";
      ret = DTCP_STACK_STOP_RUNNING;
      break;
    case DTCP_STACK_INITIALISING:
      stack_status = "INITIALISING";
      break;
    default:
      stack_status = "UNKNOWN";
      ret = DTCP_STACK_STOP_RUNNING;
      break;
  }

  GST_INFO_OBJECT (filter, "stack instance %d: status: %s", stack_instance,
      stack_status);
  if (ret == DTCP_STACK_KEEP_RUNNING) {
    g_mutex_lock (&stack->mutex);
    if (stack->stop_running == TRUE) {
      ret = DTCP_STACK_STOP_RUNNING;
    }
    g_mutex_unlock (&stack->mutex);
  } else {
    GST_ERROR_OBJECT (filter, "Stop running with %s status", stack_status);
    GST_ELEMENT_ERROR (filter, STREAM, FAILED, ("Stop running with %s status",
            stack_status), (NULL));
  }
  return ret;
}

static gpointer
gst_dtcp2usb_stack_thread (gpointer data)
{
  GstDtcp2Usb *filter;
  DTCP_StackConfig_t config;
  int status;

  filter = (GstDtcp2Usb *) data;
  g_assert (filter != NULL);

  /* stack configuration */
  memset (&config, 0, sizeof (DTCP_StackConfig_t));
  config.DtcpRole = 'T';        /* 'T' = transmitter/source, 'R' = receiver/sink */
  config.DtcpExKey = 'S';       /* 'X' = Exchange Key, 'S' = Session Exchange Key,
                                 * 'R' = Remote Exchange Key, 'M' = Move Exchange Key
                                 */
  config.UsbRole = DTCP_STACK_USB_HOST;
  config.IncomingUsbTraffic = gst_dtcp2usb_incoming_usb_traffic;
  config.OutgoingUsbTraffic = gst_dtcp2usb_outgoing_usb_traffic;
  config.TrafficCntxt_p = (void *) filter;
  status =
      DTCP_Stack_StartInstance (DTCP_STACK_INSTANCE, &config,
      gst_dtcp2usb_stack_callback, (void *) filter);
  if (status != 0) {
    GST_ERROR_OBJECT (filter, "DTCP_Stack_StartInstance failed: %d", status);
  } else {
    GST_INFO_OBJECT (filter, "dtcp stack stopped");
  }

  return NULL;
}

static gboolean
gst_dtcp2usb_launch_stream (GstDtcp2Usb * filter)
{
  GstDtcp2UsbStack *stack;
  GError *error = NULL;
  int err;

  g_assert (filter != NULL);
  GST_INFO_OBJECT (filter, "Launch dtcp2usb stream");

  filter->stack = g_new0 (GstDtcp2UsbStack, 1);
  if (filter->stack == NULL) {
    GST_ERROR_OBJECT (filter, "Failed to allocate dtcp2usb stack");
    return FALSE;
  }
  stack = filter->stack;
  if (filter->signal_in && filter->signal_out) {
    /* Use separate devices for input + output. */
    stack->dev_out = open (filter->signal_out, O_RDWR);
    stack->dev_in = open (filter->signal_in, O_RDWR);
  } else {
    if (filter->signal_in || filter->signal_out) {
      GST_WARNING_OBJECT (filter,
          "Inconsistency: just input or output device defined, falling back to single device");
    }
    stack->dev_out = stack->dev_in = open (filter->signal, O_RDWR);
  }
  gst_object_ref (filter);
  if (stack->dev_in < 0 || stack->dev_out < 0) {
    GST_ERROR_OBJECT (filter, "Failed to open USB device");
    gst_dtcp2usb_teardown_stream (filter);
    return FALSE;
  }
  g_mutex_init (&stack->mutex);
  err = sem_init (&stack->sem, 0, 0);
  if (err != 0) {
    GST_ERROR_OBJECT (filter, "Unable to create a semaphore variable %s",
        strerror (err));
    gst_dtcp2usb_teardown_stream (filter);
    return FALSE;
  }
  stack->thread =
      g_thread_try_new ("dtcp2-stack-thread", gst_dtcp2usb_stack_thread,
      (gpointer) filter, &error);
  if (error != NULL) {
    GST_ERROR_OBJECT (filter, "Failed to create DTCP2 protocol thread %s",
        error->message);
    g_error_free (error);
    gst_dtcp2usb_teardown_stream (filter);
    return FALSE;
  }

  return TRUE;
}

static void
gst_dtcp2usb_teardown_stream (GstDtcp2Usb * filter)
{
  GST_INFO_OBJECT (filter, "Teardown dtcp2usb stream");
  if (filter && filter->stack) {
    GstDtcp2UsbStack *stack = filter->stack;

    if (stack->dev_in >= 0) {
      close (stack->dev_in);
    }

    if (stack->dev_out >= 0 && stack->dev_out != stack->dev_in) {
      close (stack->dev_out);
    }

    g_mutex_lock (&stack->mutex);
    stack->stop_running = TRUE;
    g_mutex_unlock (&stack->mutex);

    g_thread_join (stack->thread);
    sem_destroy (&stack->sem);
    g_mutex_clear (&stack->mutex);

    g_clear_pointer (&filter->stack, g_free);
    gst_object_unref (filter);
  }
}

static gboolean
gst_dtcp2usb_wait_ready_to_process (GstDtcp2Usb * filter)
{
  GstDtcp2UsbStack *stack;

  g_assert (filter != NULL);
  stack = filter->stack;
  g_assert (stack != NULL);

  /* If not in the ready state, wait for it. */
  if (!stack->ready_to_process) {
    GST_INFO_OBJECT (filter, "Wait ready to process");
    if (sem_wait (&stack->sem) != 0) {
      GST_ERROR_OBJECT (filter, "Failed to wait semaphore: %s",
          strerror (errno));
      return FALSE;
    }
    stack->ready_to_process = TRUE;
    GST_INFO_OBJECT (filter, "Got semaphore signal from dtcp stack thread");
  }
  return TRUE;
}

static gboolean
gst_dtcp2usb_drm_decrypt (GstDtcp2Usb * filter, guint8 ** data,
    gsize * data_size)
{
#ifdef ENABLE_SOC_DTCP_TA
  Decrypted decrypted;
#endif

  g_assert (filter != NULL);

  if (filter->stream_changed) {
    gst_dtcp2usb_send_upstream_event (filter);
    DestroySvbDecryptor (filter->svb_decryptor);
    filter->svb_decryptor = NULL;
  }
#ifdef ENABLE_SOC_DTCP_TA
  if (filter->drm_type && !filter->svb_decryptor) {
    filter->svb_decryptor = CreateSvbDecryptor (filter->drm_type);
  }

  if (!filter->svb_decryptor) {
    GST_ERROR_OBJECT (filter, "svb_decryptor is NULL");
    GST_ELEMENT_ERROR (filter, STREAM, DECRYPT_NOKEY, ("svb_decryptor is NULL"),
        (NULL));
    return FALSE;
  }

  if (!SvbDecrypt (filter->svb_decryptor, *data, *data_size, &decrypted)) {
    GST_ERROR_OBJECT (filter, "SvbDecrypt failed");
    GST_ELEMENT_ERROR (filter, STREAM, DECRYPT, ("SvbDecrypt failed"), (NULL));
    return FALSE;
  }

  *data = (guint8 *) decrypted.address;
  *data_size = (gsize) decrypted.size;
#endif

  return TRUE;
}

static gsize
gst_dtcp2usb_fill_encrypted_buffer (GstDtcp2Usb * filter, guint8 * data,
    gsize data_size, GstMapInfo * output_map)
{
  int status;
  uint32_t output_size, pcp_size, written;

  /* Wrap flat buffer into ring buffers */
  RingBuffer_t input_ringbuf = { data, data_size, 0, 0 };
  RingBuffer_t header_ringbuf = { output_map->data, output_map->size, 0, 0 };

  g_assert (filter != NULL);

  pcp_size = data_size;
  output_size = output_map->size;
  status = DTCP_Stack_ProcessContentInit (DTCP_STREAM_INSTANCE, &header_ringbuf,
      &output_size, NULL, NULL, &pcp_size);
  if (status != 0) {
    GST_ERROR_OBJECT (filter, "DTCP_Stack_ProcessContentInit failed (%d)",
        status);
    return 0;
  } else {
    uint32_t remaining = output_map->size - output_size;

    /* Wrap flat buffer into ring buffers */
    RingBuffer_t encrypted_ringbuf = {
      output_map->data + output_size, remaining, 0, 0
    };

    GST_TRACE_OBJECT (filter,
        "DTCP_Stack_ProcessContentInit: header %u, payload %u, padding %u",
        output_size, pcp_size, remaining - pcp_size);

    status = DTCP_Stack_ProcessContent (DTCP_STREAM_INSTANCE,
        data_size, &input_ringbuf, &remaining, &encrypted_ringbuf);
    if (status != 0) {
      GST_ERROR_OBJECT (filter, "DTCP_Stack_ProcessContent failed (%d)",
          status);
      return 0;
    }
    written = remaining + output_size;
    GST_TRACE_OBJECT (filter, "DTCP_Stack_ProcessContent: written %u", written);
    return written;
  }
}

static GstBuffer *
gst_dtcp2usb_encrypt_data (GstDtcp2Usb * filter, GstBuffer * input_buf)
{
  guint8 *data;
  gsize data_size, padded_size, output_size, written;
  GstMapInfo input_map, output_map;
  GstBuffer *output_buf;

  if (!gst_dtcp2usb_wait_ready_to_process (filter))
    return NULL;

  if (!gst_buffer_map (input_buf, &input_map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (filter, "Failed to map input buffer");
    return NULL;
  }

  data = input_map.data;
  data_size = input_map.size;
  /* Actual data position and size may be overridden by drm decryptor. */
  if (!gst_dtcp2usb_drm_decrypt (filter, &data, &data_size)) {
    gst_buffer_unmap (input_buf, &input_map);
    return NULL;
  }

  padded_size = (data_size + DTCP_BLOCK_LENGTH - 1) & ~(DTCP_BLOCK_LENGTH - 1);
  output_size = DTCP_HEADER_SIZE + padded_size;
  output_buf = gst_buffer_new_allocate (NULL, output_size, NULL);
  if (!gst_buffer_map (output_buf, &output_map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (filter, "Failed to map output buffer");
    gst_buffer_unmap (input_buf, &input_map);
    return NULL;
  }

  written =
      gst_dtcp2usb_fill_encrypted_buffer (filter, data, data_size, &output_map);
  gst_buffer_unmap (input_buf, &input_map);
  gst_buffer_unmap (output_buf, &output_map);
  if (written > 0) {
    gst_buffer_resize (output_buf, 0, written);
    gst_buffer_copy_into (output_buf, input_buf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS |
        GST_BUFFER_COPY_META, 0, -1);
  } else {
    gst_buffer_unref (output_buf);
    output_buf = NULL;
  }

  return output_buf;
}

static void
gst_dtcp2usb_send_upstream_event (GstDtcp2Usb * filter)
{
  GstPad *peer_pad;
  GstStructure *structure;
  GstEvent *event;

  peer_pad = gst_pad_get_peer (filter->sinkpad);
  if (!peer_pad)
    return;

  if (filter->inactive_stream_id) {
    structure = gst_structure_new ("unload-cenc", "stream-id", G_TYPE_STRING,
        filter->inactive_stream_id, NULL);
    event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, structure);
    gst_pad_send_event (peer_pad, event);
    GST_INFO_OBJECT (filter, "Sent unload cenc(%s) event to %s:%s",
        filter->inactive_stream_id, GST_DEBUG_PAD_NAME (peer_pad));
    g_clear_pointer (&filter->inactive_stream_id, g_free);
    filter->stream_changed = FALSE;
  }
  gst_object_unref (peer_pad);
}

static gboolean
dtcp2usb_init (GstPlugin * dtcp2usb)
{
  GST_DEBUG_CATEGORY_INIT (gst_dtcp2usb_debug, "dtcp2usb", 0,
      "DTCP2 USB source plugin");

  return gst_element_register (dtcp2usb, "dtcp2usb", GST_RANK_NONE,
      GST_TYPE_DTCP2USB);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "dtcp2usb"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dtcp2usb,
    "DTCP2 USB source plugin",
    dtcp2usb_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
