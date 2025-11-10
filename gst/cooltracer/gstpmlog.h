#ifndef __GST_PMLOG_H__
#define __GST_PMLOG_H__

void gst_pmlog_write (const char *TAG, int level, const char *fmt, ...);
void gst_pmlog_write_valist (const char *TAG, int level, const char *fmt,
    va_list arg);

#define GST_PMLOG(level, ...) gst_pmlog(GST_CAT_DEFAULT, level, __VA_ARGS__)
#define GST_PMLOG_CAT(cat, level, ...) gst_pmlog(cat, level, __VA_ARGS__)

#define GST_PMLOG_VERBOSE(...) gst_pmlog(GST_CAT_DEFAULT, GST_LEVEL_LOG, __VA_ARGS__)
#define GST_PMLOG_CAT_VERBOSE(cat,...) gst_pmlog(cat, GST_LEVEL_LOG, __VA_ARGS__)

#define GST_PMLOG_DEBUG(...) gst_pmlog(GST_CAT_DEFAULT, GST_LEVEL_DEBUG, __VA_ARGS__)
#define GST_PMLOG_CAT_DEBUG(cat,...) gst_pmlog(cat, GST_LEVEL_DEBUG, __VA_ARGS__)

#define GST_PMLOG_INFO(...) gst_pmlog(GST_CAT_DEFAULT, GST_LEVEL_INFO, __VA_ARGS__)
#define GST_PMLOG_CAT_INFO(cat,...) gst_pmlog(cat, GST_LEVEL_INFO, __VA_ARGS__)

#define GST_PMLOG_ERROR(...) gst_pmlog(GST_CAT_DEFAULT, GST_LEVEL_ERROR, __VA_ARGS__)
#define GST_PMLOG_CAT_ERROR(cat,...) gst_pmlog(cat, GST_LEVEL_ERROR, __VA_ARGS__)


#define gst_pmlog(CAT, LEVEL, ...)\
  do {\
    gst_pmlog_write(gst_debug_category_get_name(CAT), LEVEL, __VA_ARGS__);\
  } while(0)

#define gst_pmlog_valist(CAT, LEVEL, ...)\
  do {\
    gst_pmlog_write_valist(gst_debug_category_get_name(CAT), LEVEL, __VA_ARGS__);\
  } while(0)


#endif
