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
/* #include <gst/base/base.h> */
/* #include <gst/controller/controller.h> */
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <sys/time.h>

#include "gsttimecodeoverlay.h"

GST_DEBUG_CATEGORY_STATIC (gst_timecodeoverlay_debug);
#define GST_CAT_DEFAULT gst_timecodeoverlay_debug

/* Filter signals and args */

enum
{
  PROP_0
};

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
/* G_DEFINE_TYPE_WITH_CODE (Gsttimecodeoverlay, gst_timecodeoverlay, 0, */
/*   GST_DEBUG_CATEGORY_INIT (gst_timecodeoverlay_debug_category, "timecodeoverlay", 0, */
  /* "debug category for timecodeoverlay element")); */
/* G_DEFINE_TYPE (Gsttimecodeoverlay, gst_timecodeoverlay, GST_TYPE_VIDEO_FILTER); */
/* GST_ELEMENT_REGISTER_DEFINE (timecodeoverlay, "timecodeoverlay", GST_RANK_NONE, */
/*     GST_TYPE_TIMECODEOVERLAY); */

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
  overlay->latency = GST_CLOCK_TIME_NONE;
}

static gboolean
gst_timecodeoverlay_src_event (GstBaseTransform * basetransform, GstEvent * event)
{
  Gsttimecodeoverlay *filter = GST_TIMECODEOVERLAY (basetransform);

  if (GST_EVENT_TYPE (event) == GST_EVENT_LATENCY) {
    GstClockTime latency = GST_CLOCK_TIME_NONE;
    gst_event_parse_latency (event, &latency);
    GST_OBJECT_LOCK (filter);
    filter->latency = latency;
    GST_OBJECT_UNLOCK (filter);
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
  // Gsttimecodeoverlay *overlay = GST_TIMECODEOVERLAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_timecodeoverlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  // Gsttimecodeoverlay *overlay = GST_TIMECODEOVERLAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
draw_timestamp(int lineoffset, GstClockTime timestamp, Gsttimecodeoverlay *overlay, GstVideoFrame *frame)
{
  guchar *y = frame->data[0];
  guchar *u = y + frame->info.offset[1];
  guchar *v = y + frame->info.offset[2];

  // TODO: Should be properties
  uint y_pos = 52;
  uint x_pos = 1920 - 896;
  gint pxsize = 16; // 1

  uint y_offset = (y_pos + lineoffset * pxsize) * frame->info.stride[0]   + x_pos*8;
  uint u_offset = (y_pos + lineoffset * pxsize) * frame->info.stride[1]/2 + x_pos*4;
  uint v_offset = (y_pos + lineoffset * pxsize) * frame->info.stride[2]/2 + x_pos*4;

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

  GST_DEBUG_OBJECT (overlay, "transform_frame_ip");

  GstClockTime buffer_time = GST_BUFFER_TIMESTAMP (frame->buffer);

  if (!GST_CLOCK_TIME_IS_VALID (buffer_time)) {
    GST_DEBUG_OBJECT (filter, "Can't draw timestamps: buffer timestamp is invalid");
    return GST_FLOW_OK;
  }

  if (frame->info.stride[0] < (8 * frame->info.finfo->pixel_stride[0] * 64)) {
    GST_WARNING_OBJECT (filter, "Can't draw timestamps: video-frame is to narrow");
    return GST_FLOW_OK;
  }

  GstSegment *segment = &GST_BASE_TRANSFORM (overlay)->segment;
  GstClockTime stream_time = gst_segment_to_stream_time (segment, GST_FORMAT_TIME, buffer_time);
  GstClockTime running_time = gst_segment_to_running_time (segment, GST_FORMAT_TIME, buffer_time);
  GstClockTime clock_time = running_time + gst_element_get_base_time (GST_ELEMENT (overlay));

  GstClockTime latency = overlay->latency;
  GstClockTime render_time = clock_time;
  if (GST_CLOCK_TIME_IS_VALID (latency))
    render_time = clock_time + latency;

  draw_timestamp (0, buffer_time, overlay, frame);
  draw_timestamp (1, stream_time, overlay, frame);
  draw_timestamp (2, running_time, overlay, frame);
  draw_timestamp (3, clock_time, overlay, frame);
  draw_timestamp (4, render_time, overlay, frame);

  draw_timestamp(5, overlay->sec_offset, overlay, frame);

  struct timeval tv;
  gettimeofday(&tv,NULL);
  unsigned long time_ms = 1000000 * (tv.tv_sec - overlay->sec_offset) + tv.tv_usec;
  draw_timestamp(6, time_ms, overlay, frame);

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
