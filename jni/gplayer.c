/*
 * gplayer.c
 *
 *  Created on: 27 lip 2015
 *      Author: Krzysztof Gawrys
 */

#include <jni.h>
#include <time.h>
#include "include/gplayer.h"

void buffer_size(CustomData *data, int size) {
	guint maxsizebytes;
	g_object_get(data->source1, "buffer-size", &maxsizebytes, NULL);

	if (size != maxsizebytes) {
		GPlayerDEBUG("Set buffer size to %i", size);
		g_object_set(data->source1, "buffer-size", (guint) size, NULL);
		g_object_set(data->source1, "use-buffering", (gboolean) TRUE, NULL);
		g_object_set(data->source1, "download", (gboolean) TRUE, NULL);
		g_object_set(data->source2, "buffer-size", (guint) size, NULL);
		g_object_set(data->source2, "use-buffering", (gboolean) TRUE, NULL);
		g_object_set(data->source2, "download", (gboolean) TRUE, NULL);
/*
		g_object_set(data->buffer, "use-buffering", (gboolean) TRUE, NULL);
		g_object_set(data->buffer, "use-rate-estimate", (gboolean) FALSE, NULL);
		g_object_set(data->buffer, "max-size-bytes", (guint) size, NULL);
		g_object_set(data->buffer, "max-size-buffers", (guint) 100 * 15, NULL);
		g_object_set(data->buffer, "max-size-time", (guint64) 15000000000, NULL);
        g_object_set(data->buffer, "low-percent", (gint) 99, NULL);
*/

		g_object_set(data->prebuf1, "max-size-bytes", (guint) size, NULL);
		g_object_set(data->prebuf1, "use-buffering", (gboolean) TRUE, NULL);
		g_object_set(data->prebuf1, "use-rate-estimate", (gboolean) FALSE, NULL);
		g_object_set(data->prebuf1, "max-size-buffers", (guint) 100 * 15, NULL);
		g_object_set(data->prebuf1, "max-size-time", (guint64) 15000000000, NULL);
        g_object_set(data->prebuf1, "low-percent", (gint) 99, NULL);
		g_object_set(data->prebuf2, "max-size-bytes", (guint) size, NULL);
		g_object_set(data->prebuf2, "use-buffering", (gboolean) TRUE, NULL);
		g_object_set(data->prebuf2, "use-rate-estimate", (gboolean) FALSE, NULL);
		g_object_set(data->prebuf2, "max-size-buffers", (guint) 100 * 15, NULL);
		g_object_set(data->prebuf2, "max-size-time", (guint64) 15000000000, NULL);
        g_object_set(data->prebuf2, "low-percent", (gint) 99, NULL);
	}
}

static gboolean gst_notify_time_cb(CustomData *data) {

	/* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
	if (!data || !data->pipeline)
		return TRUE;

	/* If we didn't know it yet, query the stream duration */
	if (!GST_CLOCK_TIME_IS_VALID(data->duration)) {
		if (!gst_element_query_duration(data->pipeline, GST_FORMAT_TIME,
				&data->duration)) {
			data->duration = 0;
		}
	}

	if (!gst_element_query_position(data->pipeline, GST_FORMAT_TIME,
			&data->position)) {
		data->position = 0;
	}

	if (data->target_state >= GST_STATE_PLAYING) {
		gplayer_notify_time(data, (int) (data->position / GST_MSECOND));
	}
	return TRUE;
}

static gboolean gst_worker_cb(CustomData *data) {
	gint64 current = -1;

	/* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
	if (!data || !data->pipeline)
		return TRUE;

	guint maxsizebytes = 0;
	guint currentlevelbytes = 0;
	guint maxprebuf1 = 0;
	guint currentprebuf1 = 0;
	guint maxprebuf2 = 0;
	guint currentprebuf2 = 0;
	guint currentbufferspercent = 0;
	guint currentprebuf1percent = 0;
	guint currentprebuf2percent = 0;

	g_object_get(data->buffer, "max-size-bytes", &maxsizebytes, NULL);
	g_object_get(data->buffer, "current-level-bytes", &currentlevelbytes, NULL);
	g_object_get(data->prebuf1, "max-size-bytes", &maxprebuf1, NULL);
	g_object_get(data->prebuf1, "current-level-bytes", &currentprebuf1, NULL);
	g_object_get(data->prebuf2, "max-size-bytes", &maxprebuf2, NULL);
	g_object_get(data->prebuf2, "current-level-bytes", &currentprebuf2, NULL);

	if (maxsizebytes > 0) {
		currentbufferspercent = (guint)(currentlevelbytes * 100 / maxsizebytes);
	}
	if (maxprebuf1 > 0) {
		currentprebuf1percent = (guint)(currentprebuf1 * 100 / maxprebuf1);
	}
	if (maxprebuf2 > 0) {
		currentprebuf2percent = (guint)(currentprebuf2 * 100 / maxprebuf2);
	}

	GPlayerDEBUG("%u %u %u %u %u %u %u\n", maxprebuf1, currentprebuf1, maxprebuf2, currentprebuf2, currentbufferspercent, currentprebuf1percent, currentprebuf2percent);

	if (!gst_element_query_duration(data->pipeline, GST_FORMAT_TIME,
			&data->duration)) {
		data->duration = 0;
	}

	count_buffer_fill++;

	guint bufferinglevel, oldbufferinglevel;

	if(data->use_main_source) {
		bufferinglevel = currentprebuf1percent;
		oldbufferinglevel = currentprebuf2percent;
	} else {
		bufferinglevel = currentprebuf2percent;
		oldbufferinglevel = currentprebuf1percent;
	}

	if (((data->use_main_source && currentprebuf1percent < 50) || (!data->use_main_source && currentprebuf2percent < 50)) && !data->internal_error) {
		no_buffer_fill++;
	}

	GstState state;
	gst_element_get_state(data->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
	if (state != data->target_state) {
		data->is_live = (gst_element_set_state(data->pipeline,
				data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
	}


	if ((count_buffer_fill == 20) && (data->position > last_position + 1999)) {
		count_buffer_fill = 0;
		last_position = data->position;
		if (no_buffer_fill > 16 && !data->wait_for_pad && !data->source_change) {
			no_buffer_fill = 0;
			data->internal_error = TRUE;
			GPlayerDEBUG("!!!!!!!!!!!! data drained... !!!!!!!!!!!!!!!\n");
			if (data->use_main_source) {
				g_object_set(data->ident1, "drop-probability", (gfloat) 1, NULL);
				g_object_set(data->ident2, "drop-probability", (gfloat) 0, NULL);
				data->use_main_source = FALSE;
				data->wait_for_pad = TRUE;
				gst_element_sync_state_with_parent (data->source2);
				gst_element_sync_state_with_parent (data->prebuf2);
			} else {
				g_object_set(data->ident1, "drop-probability", (gfloat) 0, NULL);
				g_object_set(data->ident2, "drop-probability", (gfloat) 1, NULL);
				data->use_main_source = TRUE;
				data->wait_for_pad = TRUE;
				gst_element_sync_state_with_parent (data->source1);
				gst_element_sync_state_with_parent (data->prebuf1);
			}
			no_buffer_fill = 0;
		} else {
			no_buffer_fill = 0;
		}
	}

	if (data->source_change && !data->wait_for_pad && data->internal_error) {
		gboolean change = FALSE;

		if (data->use_main_source && currentprebuf1 > 25) {
			GPlayerDEBUG("Change source to %s\n",
					GST_ELEMENT_NAME(data->source1));
			g_object_set(data->ident1, "sync", FALSE, NULL);
			g_object_set(data->ident2, "sync", FALSE, NULL);
			change = TRUE;
		}

		if (!data->use_main_source && currentprebuf2 > 25) {
			GPlayerDEBUG("Change source to %s\n",
					GST_ELEMENT_NAME(data->source2));
			g_object_set(data->ident2, "sync", FALSE, NULL);
			g_object_set(data->ident1, "sync", FALSE, NULL);
			change = TRUE;
		}

		if (data->use_main_source && currentprebuf1 > 50) {
			g_object_set(data->ident1, "drop-probability", (gfloat) 0, NULL);
		}

		if (!data->use_main_source && currentprebuf2 > 50) {
			g_object_set(data->ident2, "drop-probability", (gfloat) 0, NULL);
		}

		if (change) {
			GPlayerDEBUG("data->source_change = FALSE\n");
			data->source_change = FALSE;
			data->internal_error = FALSE;
			GstPad *active_pad, *new_pad;
			gint nb_sources;
			gchar *active_name;
			g_object_get (G_OBJECT(data->inputselector), "n-pads", &nb_sources, NULL);
			g_object_get (G_OBJECT(data->inputselector), "active-pad", &active_pad, NULL);
			active_name = gst_pad_get_name (active_pad);
			GPlayerDEBUG("G_OBJECT(data->inputselector) active-pad: %s of %i pads\n", active_name, nb_sources);
			if (strcmp(active_name, "sink_0") == 0) {
				new_pad = gst_element_get_static_pad(data->inputselector, "sink_1");
			} else {
				new_pad = gst_element_get_static_pad(data->inputselector, "sink_0");
			}
			g_object_set(G_OBJECT(data->inputselector), "active-pad", new_pad, NULL);
			g_object_get (G_OBJECT(data->inputselector), "active-pad", &active_pad, NULL);
			active_name = gst_pad_get_name (active_pad);
			GPlayerDEBUG("G_OBJECT(data->inputselector) active-pad: %s of %i pads\n", active_name, nb_sources);
			g_free (active_name);
			gst_object_unref (new_pad);
			buffer_size(data, DEFAULT_BUFFER);
			gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
		}
	}

/*	if (count_buffer_fill == 20) {
		count_buffer_fill = 0;
		if (no_buffer_fill >= 16) {
			gplayer_error(2, data);
		}
		no_buffer_fill = 0;
	}*/

	GPlayerDEBUG("buf. err.: %i, real percent.: %u, uri buf.: %i, clby: %u [%u], dur: %ld\n", no_buffer_fill, currentbufferspercent, bufferinglevel, currentlevelbytes, maxsizebytes, data->duration);

	if (data->network_error == TRUE) {
/*
		GPlayerDEBUG("Retrying setting state to PLAYING");
		data->target_state = GST_STATE_PLAYING;
		if (!data->wait_for_pad) {
			data->is_live = (gst_element_set_state(data->pipeline,
					GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
		}
*/
	}
	return TRUE;
}

/* Perform seek, if we are not too close to the previous seek. Otherwise, schedule the seek for
 * some time in the future. */
void execute_seek(gint64 desired_position, CustomData *data) {
	gint64 diff;

	if (desired_position == GST_CLOCK_TIME_NONE || !data->allow_seek || data->duration == -1)
		return;

	diff = gst_util_get_timestamp() - data->last_seek_time;

	if (!data->is_live) {
		GPlayerDEBUG("Seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS(desired_position));
		data->last_seek_time = gst_util_get_timestamp();
		gst_element_seek_simple(data->pipeline, GST_FORMAT_TIME,
				GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, desired_position);
	}
	data->desired_position = GST_CLOCK_TIME_NONE;
}

/* Retrieve errors from the bus and show them on the UI */
static void error_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
	GError *err;
	gchar *debug_info;
	gchar *message_string;

	gst_message_parse_error(msg, &err, &debug_info);
	GPlayerDEBUG("ERROR from element %s: %s\n", GST_OBJECT_NAME(msg->src),
			err->message);
	GPlayerDEBUG("Debugging info: %s\n", (debug_info) ? debug_info : "none");
	if (strcmp(err->message, "Could not establish connection to server.") == 0) {
	} else if (strcmp(err->message, "Not Found") == 0
			|| (strstr(err->message, "Internal") != NULL && strstr(err->message, "error") != NULL)) {
//		data->buffering_level = 0;
		//data->internal_error = TRUE;
/*
		if (strcmp(GST_OBJECT_NAME(msg->src), GST_OBJECT_NAME(data->source))) {
			gplayer_error(err->code, data);
		}

		data->target_state = GST_STATE_NULL;
		data->is_live = (gst_element_set_state(data->pipeline,
				data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
*/
	}
	g_error_free(err);
	g_free(debug_info);
	if (data->target_state > GST_STATE_PAUSED) {
		data->network_error = TRUE;
	}
}

void print_one_tag(const GstTagList * list, const gchar * tag, CustomData *data) {
	int i, num;

	num = gst_tag_list_get_tag_size(list, tag);
	for (i = 0; i < num; ++i) {
		const GValue *val;

		/* Note: when looking for specific tags, use the gst_tag_list_get_xyz() API,
		 * we only use the GValue approach here because it is more generic */
		val = gst_tag_list_get_value_index(list, tag, i);
		if (G_VALUE_HOLDS_STRING(val)) {
			GPlayerDEBUG("\t%20s : %s\n", tag, g_value_get_string(val));
			if (strcmp(tag, "title") == 0) {
				gplayer_metadata_update(data, g_value_get_string(val));
			}
		} else if (G_VALUE_HOLDS_UINT(val)) {
			GPlayerDEBUG("\t%20s : %u\n", tag, g_value_get_uint(val));
		} else if (G_VALUE_HOLDS_DOUBLE(val)) {
			GPlayerDEBUG("\t%20s : %g\n", tag, g_value_get_double(val));
		} else if (G_VALUE_HOLDS_BOOLEAN(val)) {
			GPlayerDEBUG("\t%20s : %s\n", tag,
					(g_value_get_boolean(val)) ? "true" : "false");
		} else if (GST_VALUE_HOLDS_BUFFER(val)) {
			GstBuffer *buf = gst_value_get_buffer(val);
			guint buffer_size = gst_buffer_get_size(buf);

			GPlayerDEBUG("\t%20s : buffer of size %u\n", tag, buffer_size);
		} else if (GST_VALUE_HOLDS_DATE_TIME(val)) {
			GstDateTime *dt = g_value_get_boxed(val);
			gchar *dt_str = gst_date_time_to_iso8601_string(dt);

			GPlayerDEBUG("\t%20s : %s\n", tag, dt_str);
			g_free(dt_str);
		} else {
			GPlayerDEBUG("\t%20s : tag of type '%s'\n", tag,
					G_VALUE_TYPE_NAME(val));
		}
	}
}

static void tag_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
	GstTagList *tags = NULL;
	gst_message_parse_tag(msg, &tags);
	GPlayerDEBUG("Got tags from element %s:\n", GST_OBJECT_NAME(msg->src));
	gst_tag_list_foreach(tags, (GstTagForeachFunc) print_one_tag, data);
	gst_tag_list_unref(tags);
	gst_message_unref(msg);
}

/* Called when the End Of the Stream is reached. Just move to the beginning of the media and pause. */
static void eos_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
	if (!data->network_error && data->target_state >= GST_STATE_PLAYING) {
		data->target_state = GST_STATE_PAUSED;
		data->is_live = (gst_element_set_state(data->pipeline, GST_STATE_PAUSED)
				== GST_STATE_CHANGE_NO_PREROLL);
		gplayer_playback_complete(data);
	} else {
		data->is_live = (gst_element_set_state(data->pipeline, GST_STATE_NULL)
				== GST_STATE_CHANGE_NO_PREROLL);
	}
}

/* Called when the duration of the media changes. Just mark it as unknown, so we re-query it in the next UI refresh. */
static void duration_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
	data->duration = GST_CLOCK_TIME_NONE;
}

/* Called when buffering messages are received. We inform the UI about the current buffering level and
 * keep the pipeline paused until 100% buffering is reached. At that point, set the desired state. */
static void buffering_cb(GstBus *bus, GstMessage *msg, CustomData *data) {

	if (data->is_live)
		return;
	if (strcmp(GST_ELEMENT_NAME(msg->src), "prebuf") == 0) {
		gst_message_parse_buffering(msg, &data->buffering_level);
		if (data->buffering_level > 75
				&& data->target_state >= GST_STATE_PLAYING) {
			buffer_size(data, DEFAULT_BUFFER);
			last_position = 0;
			data->target_state = GST_STATE_PLAYING;
			GstState state;
			gst_element_get_state(data->pipeline, &state, NULL,
					GST_CLOCK_TIME_NONE);
			if (!data->wait_for_pad) {
				if (state != GST_STATE_PLAYING) {
					data->is_live = (gst_element_set_state(data->pipeline,
							GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
				}
			}
		}
	}
}

/* Called when the clock is lost */
static void clock_lost_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
	if (data->target_state >= GST_STATE_PLAYING) {
		data->is_live = (gst_element_set_state(data->pipeline, GST_STATE_PAUSED)
				== GST_STATE_CHANGE_NO_PREROLL);
		data->is_live = (gst_element_set_state(data->pipeline,
				GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
	}
}

/* Notify UI about pipeline state changes */
static void state_changed_cb(GstBus *bus, GstMessage *msg, CustomData *data) {
	GstState old_state, new_state, pending_state;
	gst_message_parse_state_changed(msg, &old_state, &new_state,
			&pending_state);
	/* Only pay attention to messages coming from the pipeline, not its children */
	if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->pipeline)) {
		data->state = new_state;

		/* The Ready to Paused state change is particularly interesting: */
		if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
			/* If there was a scheduled seek, perform it now that we have moved to the Paused state */
			if (GST_CLOCK_TIME_IS_VALID(data->desired_position))
				execute_seek(data->desired_position, data);
		}
		if (new_state == GST_STATE_PLAYING) {
			data->network_error = FALSE;
			gplayer_playback_running(data);
		} else {
			if (data->target_state == GST_STATE_PLAYING) {
				data->is_live = (gst_element_set_state(data->pipeline,
						GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
			}
		}
	}
}

/* Check if all conditions are met to report GStreamer as initialized.
 * These conditions will change depending on the application */
void check_initialization_complete(CustomData *data) {
	JNIEnv *env = get_jni_env();
	if (!data->initialized && data->main_loop) {
		GPlayerDEBUG(
				"Initialization complete, notifying application. main_loop:%p",
				data->main_loop);
		gplayer_notify_init_complete(data);
		data->initialized = TRUE;
	}
}

/* This function will be called by the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *new_pad,
		CustomData *data) {
	GstPad *sink_pad, *src_pad;
	if (data->use_main_source) {
		sink_pad = gst_element_get_static_pad(data->prebuf1, "sink");
/*		if (strcmp(GST_ELEMENT_NAME(src), "source2") == 0) {
			GPlayerDEBUG("Sorry, this source is not allowed to push pads!\n");
			goto exit;
		}*/
	} else {
		sink_pad = gst_element_get_static_pad(data->prebuf2, "sink");
/*		if (strcmp(GST_ELEMENT_NAME(src), "source1") == 0) {
			GPlayerDEBUG("Sorry, this source is not allowed to push pads!\n");
			goto exit;
		}*/
	}

	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;

	GPlayerDEBUG("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad),
			GST_ELEMENT_NAME(src));

	/* If our converter is already linked, we have nothing to do here */
	if (gst_pad_is_linked(sink_pad)) {
		GPlayerDEBUG("  We are already linked. Ignoring.\n");
		goto exit;
	}

	/* Check the new pad's type */
	new_pad_caps = gst_pad_query_caps(new_pad, NULL);
	new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
	new_pad_type = gst_structure_get_name(new_pad_struct);
	if (!g_str_has_prefix(new_pad_type, "audio/x-raw")) {
		GPlayerDEBUG("  It has type '%s' which is not raw audio. Ignoring.\n",
				new_pad_type);
		goto exit;
	}

	/* Attempt the link */
	ret = gst_pad_link(new_pad, sink_pad);
	if (GST_PAD_LINK_FAILED(ret)) {
		GPlayerDEBUG("  Type is '%s' but link failed.\n", new_pad_type);
		gplayer_error(-1, data);
		data->target_state = GST_STATE_NULL;
	} else {
		GPlayerDEBUG("  Link succeeded (type '%s').\n", new_pad_type);
		if (data->wait_for_pad == TRUE) {
			GPlayerDEBUG("data->source_change = TRUE\n");
			data->source_change = TRUE;
		}
		data->wait_for_pad = FALSE;
	}

	if (data->use_main_source) {
		g_object_set(data->ident1, "drop-probability", (gfloat) 0, NULL);
		g_object_set(data->ident2, "drop-probability", (gfloat) 1, NULL);
/*
		if (!gst_element_link(data->prebuf1, data->ident1)) {
			GPlayerDEBUG("Elements prebuf1&ident1 could not be linked.\n");
		}
*/
	} else {
		g_object_set(data->ident1, "drop-probability", (gfloat) 1, NULL);
		g_object_set(data->ident2, "drop-probability", (gfloat) 0, NULL);
/*
		if (!gst_element_link(data->prebuf2, data->ident2)) {
			GPlayerDEBUG("Elements prebuf2&ident2 could not be linked.\n");
		}
*/
	}
//	data->input_pad = gst_element_get_compatible_pad(data->inputselector, new_pad, new_pad_caps);

	exit:
	/* Unreference the new pad's caps, if we got them */
	if (new_pad_caps != NULL)
		gst_caps_unref(new_pad_caps);

	/* Unreference the sink pad */
	gst_object_unref(sink_pad);
}

void build_pipeline(CustomData *data) {
	GstBus *bus;
	GSource *bus_source;
	GError *error = NULL;
	guint flags;

	count_buffer_fill = 0;
	no_buffer_fill = 0;

	gst_element_set_state(data->pipeline, GST_STATE_NULL);
	gst_object_unref(data->pipeline);

	data->use_main_source = TRUE;
	data->wait_for_pad = FALSE;
	GPlayerDEBUG("data->source_change = FALSE\n");
	data->source_change = FALSE;
	data->pipeline = gst_pipeline_new("test-pipeline");

	/* Build pipeline */
	data->source1 = gst_element_factory_make("uridecodebin", "source1");
	data->prebuf1 = gst_element_factory_make("queue2", "prebuf1");
	data->source2 = gst_element_factory_make("uridecodebin", "source2");
	data->prebuf2 = gst_element_factory_make("queue2", "prebuf2");
	data->ident1 = gst_element_factory_make("identity", "sink0_sync");
	data->ident2 = gst_element_factory_make("identity", "sink1_sync");
	data->inputselector = gst_element_factory_make("input-selector", "mix");
	data->resample = gst_element_factory_make("audioresample", "resample");
	data->convert = gst_element_factory_make("audioconvert", "convert");
	data->sink = gst_element_factory_make("autoaudiosink", "sink");

	g_object_set(data->ident1, "drop-probability", (gfloat) 0, NULL);
	g_object_set(data->ident2, "drop-probability", (gfloat) 1, NULL);

	if (!data->pipeline || !data->resample || !data->source1 || !data->source2 || !data->prebuf1 || !data->prebuf2 || !data->ident1 || !data->ident2 || !data->inputselector || !data->convert || !data->sink) {
		gplayer_error(-1, data);
		GPlayerDEBUG("Not all elements could be created.\n");
		return;
	}

	gst_bin_add_many(GST_BIN(data->pipeline), data->source1, data->prebuf1, data->source2, data->prebuf2, data->ident1, data->ident2, data->inputselector, data->convert, data->resample, data->sink, NULL);

	if (!gst_element_link(data->prebuf1, data->ident1)) {
		GPlayerDEBUG("Elements prebuf1&ident1 could not be linked.\n");
		gst_object_unref(data->pipeline);
	}

	if (!gst_element_link(data->prebuf2, data->ident2)) {
		GPlayerDEBUG("Elements prebuf2&ident1 could not be linked.\n");
		gst_object_unref(data->pipeline);
	}

	if (!gst_element_link(data->ident1, data->inputselector)) {
		GPlayerDEBUG("Elements ident1 could not be linked.\n");
		gst_object_unref(data->pipeline);
	}

	if (!gst_element_link(data->ident2, data->inputselector)) {
		GPlayerDEBUG("Elements ident2 could not be linked.\n");
		gst_object_unref(data->pipeline);
	}

	GstPad *buf_pad = gst_element_get_static_pad(data->convert, "sink");
	GstPad *mix_pad = gst_element_get_static_pad(data->inputselector, "src");
	GstPadLinkReturn ret = gst_pad_link(mix_pad, buf_pad);
	if (GST_PAD_LINK_FAILED(ret)) {
		GPlayerDEBUG("Elements inputselector&convert could not be linked.\n");
		gst_object_unref(data->pipeline);
	}

	if (!gst_element_link(data->convert, data->resample)) {
		GPlayerDEBUG("Elements convert&resample could not be linked.\n");
		gst_object_unref(data->pipeline);
	}
	if (!gst_element_link(data->resample, data->sink)) {
		GPlayerDEBUG("Elements resample&sink could not be linked.\n");
		gst_object_unref(data->pipeline);
	}

	buffer_size(data, SMALL_BUFFER);

	g_signal_connect(data->source1, "pad-added", (GCallback) pad_added_handler,
			data);

	data->target_state = GST_STATE_READY;
	gst_element_set_state(data->pipeline, GST_STATE_READY);

	bus = gst_element_get_bus(data->pipeline);
	bus_source = gst_bus_create_watch(bus);
	g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func,
			NULL, NULL);
	if (data->timeout_worker) {
		g_source_destroy(data->timeout_worker);
	}
	data->timeout_worker = g_timeout_source_new(WORKER_TIMEOUT);
	g_source_set_callback(data->timeout_worker,
			(GSourceFunc) gst_worker_cb, data, NULL);
	g_source_attach(data->timeout_worker, data->context);
	g_source_attach(bus_source, data->context);
	g_source_unref(bus_source);
	g_source_unref(data->timeout_worker);
	g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) error_cb,
			data);
	g_signal_connect(G_OBJECT(bus), "message::eos", (GCallback) eos_cb, data);
	g_signal_connect(G_OBJECT(bus), "message::tag", (GCallback) tag_cb, data);
	g_signal_connect(G_OBJECT(bus), "message::state-changed",
			(GCallback) state_changed_cb, data);
	g_signal_connect(G_OBJECT(bus), "message::duration",
			(GCallback) duration_cb, data);
	g_signal_connect(G_OBJECT(bus), "message::buffering",
			(GCallback) buffering_cb, data);
	g_signal_connect(G_OBJECT(bus), "message::clock-lost",
			(GCallback) clock_lost_cb, data);
	gst_object_unref(bus);

}

/* Main method for the native code. This is executed on its own thread. */
static void *app_function(void *userdata) {
	CustomData *data = (CustomData *) userdata;

	GPlayerDEBUG("Creating pipeline in CustomData at %p", data);

	/* Create our own GLib Main Context and make it the default one */
	data->context = g_main_context_new();
	g_main_context_push_thread_default(data->context);

	data->pipeline = gst_pipeline_new("test-pipeline");

	build_pipeline(data);

	/* Create a GLib Main Loop and set it to run */
	GPlayerDEBUG("Entering main loop... (CustomData:%p)", data);
	data->main_loop = g_main_loop_new(data->context, FALSE);
	check_initialization_complete(data);
	g_main_loop_run(data->main_loop);
	GPlayerDEBUG("Exited main loop");
	g_main_loop_unref(data->main_loop);
	data->main_loop = NULL;

	/* Free resources */
	g_main_context_pop_thread_default(data->context);
	g_main_context_unref(data->context);
	data->target_state = GST_STATE_NULL;
	gst_element_set_state(data->pipeline, GST_STATE_NULL);
	gst_object_unref(data->pipeline);

	return NULL;
}

/*
 * Java Bindings
 */

void set_notifyfunction(CustomData *data) {
	GstBus *bus;
	GSource *bus_source;

	if (data->notify_time > 0) {
		bus = gst_element_get_bus(data->pipeline);
		bus_source = gst_bus_create_watch(bus);
		g_source_set_callback(bus_source,
				(GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
		g_source_attach(bus_source, data->context);
		g_source_unref(bus_source);

		if (data->timeout_source) {
			g_source_destroy(data->timeout_source);
		}
		/* Register a function that GLib will call 4 times per second */
		data->timeout_source = g_timeout_source_new(data->notify_time);
		g_source_set_callback(data->timeout_source,
				(GSourceFunc) gst_notify_time_cb, data, NULL);
		g_source_attach(data->timeout_source, data->context);
		g_source_unref(data->timeout_source);
	}
}

/* Instruct the native code to create its internal data structure, pipeline and thread */
void gst_native_init(JNIEnv* env, jobject thiz) {
	CustomData *data = g_new0(CustomData, 1);
	data->last_seek_time = GST_CLOCK_TIME_NONE;
	SET_CUSTOM_DATA(env, thiz, custom_data_field_id, data);
	GPlayerDEBUG("Created CustomData at %p", data);
	data->app = (*env)->NewGlobalRef(env, thiz);
	GPlayerDEBUG("Created GlobalRef for app object at %p", data->app);
	pthread_create(&gst_app_thread, NULL, &app_function, data);
}

/* Quit the main loop, remove the native thread and free resources */
void gst_native_finalize(JNIEnv* env, jobject thiz) {
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	if (!data)
		return;
	GPlayerDEBUG("Quitting main loop...");
	g_main_loop_quit(data->main_loop);
	GPlayerDEBUG("Waiting for thread to finish...");
	pthread_join(gst_app_thread, NULL);
	GPlayerDEBUG("Deleting GlobalRef for app object at %p", data->app);
	(*env)->DeleteGlobalRef(env, data->app);
	GPlayerDEBUG("Freeing CustomData at %p", data);
	g_free(data);
	SET_CUSTOM_DATA(env, thiz, custom_data_field_id, NULL);
	GPlayerDEBUG("Done finalizing");
}
