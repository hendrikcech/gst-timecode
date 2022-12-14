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
 * SECTION:element-timecodeoverlay
 *
 * FIXME:Describe timecodeoverlay here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! timecodeoverlay ! fakesink silent=TRUE
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

#include "gsttimecodeoverlay.h"

GST_DEBUG_CATEGORY_STATIC (gst_timecodeoverlay_debug);
#define GST_CAT_DEFAULT gst_timecodeoverlay_debug

/* Filter signals and args */

enum
{
  PROP_0,
  PROP_LOCATION
};


static const char *default_path = "/tmp/gsttime_sndr.csv";
static const char *logfile_columns = "ts\tframe_nr\ttime_s\tsec_offset\n";
static const char *fmt_string = "%s\t%lu\t%lu\t%lu\n";

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

#define gst_timecodeoverlay_parent_class parent_class

G_DEFINE_TYPE (Gsttimecodeoverlay, gst_timecodeoverlay, GST_TYPE_VIDEO_FILTER);
GST_ELEMENT_REGISTER_DEFINE (timecodeoverlay, "timecodeoverlay", GST_RANK_NONE,
    GST_TYPE_TIMECODEOVERLAY);

static void gst_timecodeoverlay_dispose (GObject *object);
static void gst_timecodeoverlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_timecodeoverlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_timecodeoverlay_src_event (GstBaseTransform * basetransform, GstEvent * event);
static GstFlowReturn gst_timecodeoverlay_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame);

/* GObject vmethod implementations */

/* initialize the timecodeoverlay's class */
static void
gst_timecodeoverlay_class_init (GsttimecodeoverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_timecodeoverlay_set_property;
  gobject_class->get_property = gst_timecodeoverlay_get_property;

  gobject_class->dispose = gst_timecodeoverlay_dispose;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location", "Path to log file", default_path,
                           G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gst_element_class_set_details_simple (gstelement_class,
      "timecodeoverlay",
      "Generic/Filter",
      "Writes timestamps to frames",
      "Hendrik Cech <<hendrik.cech@gmail.com>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  GST_VIDEO_FILTER_CLASS (klass)->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_timecodeoverlay_transform_frame_ip);

  GST_BASE_TRANSFORM_CLASS (klass)->src_event =
      GST_DEBUG_FUNCPTR (gst_timecodeoverlay_src_event);


  /* debug category for fltering log messages
   */
  GST_DEBUG_CATEGORY_INIT (gst_timecodeoverlay_debug, "timecodeoverlay", 0,
      "Template timecodeoverlay");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_timecodeoverlay_init (Gsttimecodeoverlay * overlay)
{
  struct timeval tv;
  gettimeofday(&tv,NULL);
  overlay->sec_offset = tv.tv_sec;
  overlay->frame_nr = 0;
  overlay->latency = GST_CLOCK_TIME_NONE;

  char *path = malloc(sizeof(default_path));
  strcpy(path, default_path);
  overlay->logfile_path = path;
  overlay->logfile = g_fopen(overlay->logfile_path, "w");
  if (!overlay->logfile) {
    GST_ERROR_OBJECT (overlay, "Failed opening logfile at %s", overlay->logfile_path);
    return;
  }
}

static void
gst_timecodeoverlay_dispose (GObject *object)
{
  Gsttimecodeoverlay *filter = GST_TIMECODEOVERLAY (object);
  GST_INFO_OBJECT(filter, "Closing logfile");
  g_free(filter->logfile_path);
  if (filter->logfile)
    fclose(filter->logfile);
}

static gboolean
gst_timecodeoverlay_src_event (GstBaseTransform * basetransform, GstEvent * event)
{
  Gsttimecodeoverlay *overlay = GST_TIMECODEOVERLAY (basetransform);

  if (GST_EVENT_TYPE (event) == GST_EVENT_LATENCY) {
    GstClockTime latency = GST_CLOCK_TIME_NONE;
    gst_event_parse_latency (event, &latency);
    GST_OBJECT_LOCK (overlay);
    overlay->latency = latency;
    GST_OBJECT_UNLOCK (overlay);
    GST_INFO_OBJECT (overlay, "Latency is now %f ms (%lu ns)", latency/1e6, latency);
  }

  /* Chain up */
  return
      GST_BASE_TRANSFORM_CLASS (gst_timecodeoverlay_parent_class)->src_event
      (basetransform, event);
}

static void
gst_timecodeoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gsttimecodeoverlay *filter = GST_TIMECODEOVERLAY (object);

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
gst_timecodeoverlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gsttimecodeoverlay *filter = GST_TIMECODEOVERLAY (object);

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

static void
draw_timestamp(int lineoffset, GstClockTime timestamp, Gsttimecodeoverlay *overlay, GstVideoFrame *frame)
{
  guchar *y = frame->data[0];
  guchar *u = y + frame->info.offset[1];
  guchar *v = y + frame->info.offset[2];

  // TODO: Should be properties
  guint y_pos = 52;
  guint x_pos = 1920 - 896;
  guint pxsize = 16; // 1

  guint y_offset = (y_pos + lineoffset * pxsize) * frame->info.stride[0]   + x_pos*8;
  guint u_offset = (y_pos + lineoffset * pxsize) * frame->info.stride[1]/2 + x_pos*4;
  guint v_offset = (y_pos + lineoffset * pxsize) * frame->info.stride[2]/2 + x_pos*4;

  for (int line = 0; line < pxsize; line++) {
    if (line % 2 == 0) {
      memset(u + u_offset + frame->info.stride[1] * (line/2-2), 128, pxsize/2 * 64);
      memset(v + v_offset + frame->info.stride[2] * (line/2-2), 128, pxsize/2 * 64);
    }
    for (int bit = 0; bit < 64; bit++) {
      char y_color = ((timestamp >> (63 - bit)) & 1) * 255;
      memset(y + y_offset + frame->info.stride[0] * line + bit * pxsize, y_color, pxsize);
    }
  }
}


/* this function does the actual processing
 */
static GstFlowReturn
gst_timecodeoverlay_transform_frame_ip (GstVideoFilter * filter, GstVideoFrame * frame)
{
  Gsttimecodeoverlay *overlay = GST_TIMECODEOVERLAY (filter);

  GstClockTime buffer_time = GST_BUFFER_TIMESTAMP (frame->buffer);

  if (!GST_CLOCK_TIME_IS_VALID (buffer_time)) {
    GST_DEBUG_OBJECT (overlay, "Can't draw timestamps: buffer timestamp is invalid");
    return GST_FLOW_OK;
  }

  if (frame->info.stride[0] < (8 * frame->info.finfo->pixel_stride[0] * 64)) {
    GST_WARNING_OBJECT (overlay, "Can't draw timestamps: video-frame is to narrow");
    return GST_FLOW_OK;
  }

  /* GstSegment *segment = &GST_BASE_TRANSFORM (overlay)->segment; */
  /* GstClockTime stream_time = gst_segment_to_stream_time (segment, GST_FORMAT_TIME, buffer_time); */
  /* GstClockTime running_time = gst_segment_to_running_time (segment, GST_FORMAT_TIME, buffer_time); */
  /* GstClockTime clock_time = running_time + gst_element_get_base_time (GST_ELEMENT (overlay)); */

  /* GstClockTime latency = overlay->latency; */
  /* GstClockTime render_time = clock_time; */
  /* if (GST_CLOCK_TIME_IS_VALID (latency)) */
  /*   render_time = clock_time + latency; */

  /* draw_timestamp (0, buffer_time, overlay, frame); */
  /* draw_timestamp (1, stream_time, overlay, frame); */
  /* draw_timestamp (2, running_time, overlay, frame); */
  /* draw_timestamp (3, clock_time, overlay, frame); */
  /* draw_timestamp (4, render_time, overlay, frame); */
  struct timeval tv;
  gettimeofday(&tv,NULL);
  guint64 time_ms = 1000000 * (tv.tv_sec - overlay->sec_offset) + tv.tv_usec;

  gchar *ts = get_ts();


  #define LOG_LINE_LEN 256
  char log_line[LOG_LINE_LEN] = {0};
  snprintf (log_line, LOG_LINE_LEN, fmt_string, ts, overlay->frame_nr, time_ms, overlay->sec_offset);
  GST_LOG_OBJECT (overlay,          fmt_string, ts, overlay->frame_nr, time_ms, overlay->sec_offset);
  fputs(log_line, overlay->logfile);
  g_free(ts);

  draw_timestamp(5, overlay->sec_offset, overlay, frame);
  draw_timestamp(6, time_ms, overlay, frame);
  draw_timestamp(7, overlay->frame_nr++, overlay, frame);

  return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
timecodeoverlay_init (GstPlugin * timecodeoverlay)
{
  return GST_ELEMENT_REGISTER (timecodeoverlay, timecodeoverlay);
}

/* gstreamer looks for this structure to register timecodeoverlays
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    timecodeoverlay,
    "timecodeoverlay",
    timecodeoverlay_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
