#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstdashsink.hpp"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dashsink", GST_RANK_NONE,
          gst_dashsink_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dashsink,
    "DashSink",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
