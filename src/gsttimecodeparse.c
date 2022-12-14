/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2022 Hendrik Cech <<user@hostname.org>>
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
 * SECTION:element-timecodeparse
 *
 * FIXME:Describe timecodeparse here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! timecodeparse ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <sys/time.h>
#include <time.h>
#include <glib/gstdio.h>

#include "gsttimecodeparse.h"

GST_DEBUG_CATEGORY_STATIC (gst_timecodeparse_debug);
#define GST_CAT_DEFAULT gst_timecodeparse_debug

/* Filter signals and args */

enum
{
  PROP_0,
  PROP_LOCATION
};

static const char *default_path = "/tmp/gsttime_rcvr.csv";

static const char *logfile_columns = "ts\tframe_nr\tlatency\ttime_s\ttime_p\tsec_offset\n";
static const char *fmt_string = "%s\t%lu\t%ld\t%lu\t%lu\t%lu\n";

/* the capabilities of the inputs and outputs.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string) { I420 }")
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string) { I420 }")
);

#define gst_timecodeparse_parent_class parent_class
G_DEFINE_TYPE (Gsttimecodeparse, gst_timecodeparse, GST_TYPE_VIDEO_FILTER);
GST_ELEMENT_REGISTER_DEFINE (timecodeparse, "timecodeparse", GST_RANK_NONE,
    GST_TYPE_TIMECODEPARSE);

static gboolean gst_timecodeparse_src_event (GstBaseTransform * basetransform, GstEvent * event);
static void gst_timecodeparse_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_timecodeparse_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_timecodeparse_dispose (GObject *object);
static GstFlowReturn gst_timecodeparse_transform_frame_ip (GstVideoFilter * filter,
                                                           GstVideoFrame * frame);

/* GObject vmethod implementations */

/* initialize the timecodeparse's class */
static void
gst_timecodeparse_class_init (GsttimecodeparseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_timecodeparse_set_property;
  gobject_class->get_property = gst_timecodeparse_get_property;

  gobject_class->dispose = gst_timecodeparse_dispose;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location", "Path to log file", default_path,
                           G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gst_element_class_set_details_simple (gstelement_class,
      "timecodeparse",
      "Generic/Filter",
      "Parses time information from frames and writes log to file",
      "Hendrik Cech <<hendrik.cech@gmail.com>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  GST_BASE_TRANSFORM_CLASS (klass)->src_event =
      GST_DEBUG_FUNCPTR (gst_timecodeparse_src_event);

  GST_VIDEO_FILTER_CLASS (klass)->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_timecodeparse_transform_frame_ip);

  /* debug category for fltering log messages
   */
  GST_DEBUG_CATEGORY_INIT (gst_timecodeparse_debug, "timecodeparse", 0,
      "Parse the time code from frames");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_timecodeparse_init (Gsttimecodeparse * filter)
{
  char *path = malloc(sizeof(default_path));
  strcpy(path, default_path);
  filter->logfile_path = path;
  filter->logfile = g_fopen(filter->logfile_path, "w");
  if (!filter->logfile) {
    GST_ERROR_OBJECT (filter, "Failed opening logfile at %s", filter->logfile_path);
    return;
  }
}

static void
gst_timecodeparse_dispose (GObject *object)
{
  Gsttimecodeparse *filter = GST_TIMECODEPARSE (object);
  GST_INFO_OBJECT(filter, "Closing logfile");
  g_free(filter->logfile_path);
  if (filter->logfile)
    fclose(filter->logfile);
}

static gboolean
gst_timecodeparse_src_event (GstBaseTransform * basetransform, GstEvent * event)
{
  Gsttimecodeparse *overlay = GST_TIMECODEPARSE (basetransform);

  if (GST_EVENT_TYPE (event) == GST_EVENT_LATENCY) {
    GstClockTime latency = GST_CLOCK_TIME_NONE;
    gst_event_parse_latency (event, &latency);
    GST_INFO_OBJECT (overlay, "Latency is now %f ms (%lu ns)", latency/1e6, latency);
  }

  /* Chain up */
  return
      GST_BASE_TRANSFORM_CLASS (gst_timecodeparse_parent_class)->src_event
      (basetransform, event);
}

static void
gst_timecodeparse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gsttimecodeparse *filter = GST_TIMECODEPARSE (object);

  switch (prop_id) {
    case PROP_LOCATION: {
      gchar *path_old = filter->logfile_path;
      FILE *logfile_old = filter->logfile;
      gchar *path_new = g_value_dup_string (value);
      FILE *logfile_new = g_fopen(path_new, "w");
      if (!logfile_new) {
        GST_ERROR_OBJECT (filter, "Failed opening logfile at %s", path_new);
        return;
      }
      fputs(logfile_columns, logfile_new);
      filter->logfile_path = path_new;
      filter->logfile = logfile_new;
      g_free(path_old);
      if (logfile_old)
        fclose(logfile_old);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_timecodeparse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gsttimecodeparse *filter = GST_TIMECODEPARSE (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, filter->logfile_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gchar
*get_ts()
{
  GDateTime *dt = g_date_time_new_now_utc();
  if (dt == NULL)
    return NULL;
  gchar *ts = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
  int ms = g_date_time_get_microsecond(dt);
  int size = sizeof("2011-10-08 07:07:09.000000Z");
  char *buf = malloc(size);
  g_snprintf(buf, size, "%s.%06dZ", ts, ms);
  g_free(ts);
  g_date_time_unref(dt);
  return buf;
}

static GstClockTime
read_timestamp(int lineoffset, GstVideoFrame *frame, Gsttimecodeparse *overlay)
{
  GstClockTime timestamp = 0;

  guchar *y = frame->data[0];
  guchar *u = y + frame->info.offset[1];
  guchar *v = y + frame->info.offset[2];

  uint y_pos = 52;
  uint x_pos = 1920 - 896;
  gint pxsize = 16; // 1

  guint y_offset = (y_pos + lineoffset * pxsize) * frame->info.stride[0]   + x_pos*8;
  guint u_offset = (y_pos + lineoffset * pxsize) * frame->info.stride[1]/2 + x_pos*4;
  guint v_offset = (y_pos + lineoffset * pxsize) * frame->info.stride[2]/2 + x_pos*4;

  // Don't look at the first pixel of each bit-pixel but at the middle of it
  y_offset += pxsize/2 * frame->info.stride[0];
  u_offset += pxsize/2 * frame->info.stride[1]/2;
  v_offset += pxsize/2 * frame->info.stride[2]/2;

  guint u_sum = 0;
  guint v_sum = 0;
  for (int bit = 0; bit < 64; bit++) {
    guchar y_value = y[y_offset + bit   * pxsize + pxsize/2];
    guchar u_value = u[u_offset + bit/2 * pxsize + pxsize/2/2];
    guchar v_value = v[v_offset + bit/2 * pxsize + pxsize/2/2];
    u_sum += u_value;
    v_sum += v_value;
    GST_TRACE_OBJECT(overlay, "bit=%d: %u,%u,%u", bit, y_value, u_value, v_value);
    /* if ((y_value != 0 && y_value != 255) || u_value != 128 || v_value != 128) // corrupted */
    /*   return 0; */
    if (y_value > 20 && y_value < 230) {
      GST_TRACE_OBJECT(overlay, "ts %d discarded: y_value=%u", lineoffset, y_value);
      return 0;
    }

    timestamp |= (y_value >= 230) ?  (guint64) 1 << (63 - bit) : 0;
  }

  if ((u_sum / 64 < 100) || (u_sum /64 > 156) || (v_sum / 64 < 100) || (v_sum /64 > 156)) {
      GST_TRACE_OBJECT(overlay, "ts %d discarded: avg u_sum=%u, avg v_sum=%u",
                       lineoffset, u_sum/64, v_sum/64);
      return 0;
  }

  return timestamp;
}

typedef struct {
  GstClockTime buffer_time;
  GstClockTime stream_time;
  GstClockTime running_time;
  GstClockTime clock_time;
  GstClockTime render_time;
  guint64 sec_offset;
  guint64 render_realtime;
  guint64 frame_nr;
} Timestamps;

/* this function does the actual processing
 */
static GstFlowReturn
gst_timecodeparse_transform_frame_ip (GstVideoFilter * filter, GstVideoFrame * frame)
{
  Gsttimecodeparse *overlay = GST_TIMECODEPARSE (filter);

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (frame->buffer)))
    gst_object_sync_values (GST_OBJECT (filter), GST_BUFFER_TIMESTAMP (frame->buffer));

  GstClockTime buffer_time = GST_BUFFER_TIMESTAMP (frame->buffer);

  if (!GST_CLOCK_TIME_IS_VALID (buffer_time)) {
    GST_DEBUG_OBJECT (overlay, "Can't measure latency: buffer timestamp is "
        "invalid");
    return GST_FLOW_OK;
  }

  if (frame->info.stride[0] < (8 * frame->info.finfo->pixel_stride[0] * 64)) {
    GST_WARNING_OBJECT (overlay, "Can't read timestamps: video-frame is to narrow");
    return GST_FLOW_OK;
  }

  /* GstSegment *segment = &GST_BASE_TRANSFORM (overlay)->segment; */
  /* GstClockTime running_time = gst_segment_to_running_time (segment, GST_FORMAT_TIME, buffer_time); */
  /* GstClockTime clock_time = running_time + gst_element_get_base_time (GST_ELEMENT (overlay)); */

  Timestamps timestamps;
  /* timestamps.buffer_time = read_timestamp (0, frame, overlay); */
  /* timestamps.stream_time = read_timestamp (1, frame, overlay); */
  /* timestamps.running_time = read_timestamp (2, frame, overlay); */
  /* timestamps.clock_time = read_timestamp (3, frame, overlay); */
  /* timestamps.render_time = read_timestamp (4, frame, overlay); */
  timestamps.sec_offset = read_timestamp (5, frame, overlay);
  timestamps.render_realtime = read_timestamp (6, frame, overlay);
  timestamps.frame_nr = read_timestamp (7, frame, overlay);

  /* GST_LOG_OBJECT (overlay, "Read timestamps: buffer_time = %" GST_TIME_FORMAT */
  /*     ", stream_time = %" GST_TIME_FORMAT ", running_time = %" GST_TIME_FORMAT */
  /*     ", clock_time = %" GST_TIME_FORMAT ", render_time = %" GST_TIME_FORMAT */
  /*     ", render_realtime = %lu", */
  /*     GST_TIME_ARGS(timestamps.buffer_time), */
  /*     GST_TIME_ARGS(timestamps.stream_time), */
  /*     GST_TIME_ARGS(timestamps.running_time), */
  /*     GST_TIME_ARGS(timestamps.clock_time), */
  /*     GST_TIME_ARGS(timestamps.render_time), */
  /*     timestamps.render_realtime); */

  struct timeval tv;
  gettimeofday(&tv,NULL);
  guint64 now = 1000000 * (tv.tv_sec - timestamps.sec_offset) + tv.tv_usec;
  gchar *ts = get_ts();
  long latency = -1;
  if (timestamps.sec_offset == 0 || timestamps.render_realtime == 0) {
    GST_DEBUG_OBJECT(overlay, "Failed to read sec_offset or render_realtime");
  } else {
     latency = now - timestamps.render_realtime;
  }
  if (latency > 30*1e6 || latency < -1) {
    GST_DEBUG_OBJECT(overlay, "Discard unlikely latency (<0s or >30s): %ld", latency);
    latency = -1;
  }
  #define LOG_LINE_LEN 256
  char log_line[LOG_LINE_LEN] = {0};
  snprintf (log_line, LOG_LINE_LEN, fmt_string, ts, timestamps.frame_nr, latency, timestamps.render_realtime, now, timestamps.sec_offset);
  GST_LOG_OBJECT (overlay,          fmt_string, ts, timestamps.frame_nr, latency, timestamps.render_realtime, now, timestamps.sec_offset);
  fputs(log_line, overlay->logfile);
  g_free(ts);

  return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
timecodeparse_init (GstPlugin * timecodeparse)
{
  return GST_ELEMENT_REGISTER (timecodeparse, timecodeparse);
}

/* gstreamer looks for this structure to register timecodeparses
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    timecodeparse,
    "timecodeparse",
    timecodeparse_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
