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

#include <gst/check/gstcheck.h>
#include <gst/cool/gstcool.h>

GST_START_TEST (test_cool_init)
{

  g_setenv ("GST_COOL_CONFIG", GST_COOL_CONFIG_PATH, FALSE);

  gst_cool_init (NULL, NULL);

  gst_cool_init (NULL, NULL);
}

GST_END_TEST;

static Suite *
gstcool_suite (void)
{
  Suite *s = suite_create ("GstCool");
  TCase *tc_chain = tcase_create ("gst cool tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_cool_init);

  return s;
}

GST_CHECK_MAIN (gstcool);
