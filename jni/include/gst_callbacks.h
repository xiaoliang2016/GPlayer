/*
 * gst_callbacks.h
 *
 *  Created on: 11 wrz 2015
 *      Author: Krzysztof Gawrys
 */

// internal GStreamer Callbacks
static void buffering_cb(GstBus *bus, GstMessage *msg, CustomData *data);
static void clock_lost_cb(GstBus *bus, GstMessage *msg, CustomData *data);
static gboolean delayed_seek_cb(CustomData *data);
static void duration_cb(GstBus *bus, GstMessage *msg, CustomData *data);
static void eos_cb(GstBus *bus, GstMessage *msg, CustomData *data);
static void error_cb(GstBus *bus, GstMessage *msg, CustomData *data);
static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data);
static void state_changed_cb(GstBus *bus, GstMessage *msg, CustomData *data);
static void tag_cb(GstBus *bus, GstMessage *msg, CustomData *data);
