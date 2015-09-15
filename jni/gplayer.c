/*
 * gplayer.c
 *
 *  Created on: 27 lip 2015
 *      Author: Krzysztof Gawrys
 */

#include <jni.h>
#include "include/gplayer.h"

void buffer_size(CustomData *data, int size) {
	guint maxsizebytes;
	g_object_get(data->source, "buffer-size", &maxsizebytes, NULL);

	if (size != maxsizebytes) {
		GPlayerDEBUG("Set buffer size to %i", size);
		g_object_set(data->source, "buffer-size", (guint) size, NULL);
		g_object_set(data->source, "use-buffering", (gboolean) TRUE, NULL);
		g_object_set(data->source, "download", (gboolean) TRUE, NULL);
		g_object_set(data->buffer, "use-buffering", (gboolean) TRUE, NULL);
		g_object_set(data->buffer, "low-percent", (gint) 95, NULL);
		g_object_set(data->buffer, "use-rate-estimate", (gboolean) TRUE, NULL);
		g_object_set(data->buffer, "max-size-bytes", (guint) size, NULL);
	}
}

/* If we have pipeline and it is running, query the current position and clip duration and inform
 * the application */
static gboolean gst_notify_time_cb(CustomData *data) {
	gint64 current = -1;
	gint64 position;

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
			&position)) {
		position = 0;
	}

	if (data->target_state >= GST_STATE_PLAYING) {
		JNIEnv *env = get_jni_env();
		guint maxsizebytes;
		guint currentlevelbytes;
		g_object_get(data->buffer, "max-size-bytes", &maxsizebytes, NULL);
		g_object_get(data->buffer, "current-level-bytes", &currentlevelbytes,
				NULL);
		if (!gst_element_query_duration(data->pipeline, GST_FORMAT_TIME,
				&data->duration)) {
			data->duration = 0;
		}

		GPlayerDEBUG("Notify - buffer: %i, clbyte: %i, duration: %ld",
				maxsizebytes, currentlevelbytes, data->duration);

		gplayer_notify_time(data, (int) (position / GST_MSECOND));
		if (data->network_error == TRUE) {
			GPlayerDEBUG("Retrying setting state to PLAYING");
			data->is_live = (gst_element_set_state(data->pipeline,
					GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
		}
	}
	return TRUE;
}

/* Perform seek, if we are not too close to the previous seek. Otherwise, schedule the seek for
 * some time in the future. */
void execute_seek(gint64 desired_position, CustomData *data) {
	gint64 diff;

	if (desired_position == GST_CLOCK_TIME_NONE || !data->allow_seek)
		return;

	diff = gst_util_get_timestamp() - data->last_seek_time;

	if (!data->is_live) {
		/* Perform the seek now */
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
	if (strcmp(err->message, "Not Found") == 0
			|| strcmp(err->message, "Internal data stream error.") == 0) {
		gplayer_error(err->code, data);
		data->target_state = GST_STATE_NULL;
		data->is_live = (gst_element_set_state(data->pipeline,
				data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
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
		//data->target_state = GST_STATE_NULL;
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

	gst_message_parse_buffering(msg, &data->buffering_level);
	GPlayerDEBUG("buffering: %d", data->buffering_level);
	if (data->buffering_level > 75 && data->target_state >= GST_STATE_PLAYING) {
		buffer_size(data, DEFAULT_BUFFER);
		data->target_state = GST_STATE_PLAYING;
		data->is_live = (gst_element_set_state(data->pipeline,
				GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
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
	GstPad *sink_pad = gst_element_get_static_pad(data->buffer, "sink");
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

	if (gst_element_link(data->convert, data->sink)) {
		GPlayerDEBUG("Elements could not be linked.\n");
	}
	/* Attempt the link */
	ret = gst_pad_link(new_pad, sink_pad);
	if (GST_PAD_LINK_FAILED(ret)) {
		GPlayerDEBUG("  Type is '%s' but link failed.\n", new_pad_type);
		gplayer_error(-1, data);
		data->target_state = GST_STATE_NULL;
		data->is_live = (gst_element_set_state(data->pipeline,
				data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
	} else {
		GPlayerDEBUG("  Link succeeded (type '%s').\n", new_pad_type);
	}

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

	gst_element_set_state(data->pipeline, GST_STATE_NULL);
	gst_object_unref(data->pipeline);
	data->pipeline = gst_pipeline_new("test-pipeline");

	/* Build pipeline */
	data->source = gst_element_factory_make("uridecodebin", "source");
	data->buffer = gst_element_factory_make("queue2", "buffer");
	data->convert = gst_element_factory_make("audioconvert", "convert");
	data->sink = gst_element_factory_make("autoaudiosink", "sink");

	if (!data->pipeline || !data->source || !data->convert || !data->buffer
			|| !data->sink) {
		gplayer_error(-1, data);
		GPlayerDEBUG("Not all elements could be created.\n");
		return;
	}

	gst_bin_add_many(GST_BIN(data->pipeline), data->source, data->convert,
			data->buffer, data->sink, NULL);
	if (!gst_element_link(data->buffer, data->convert)
			|| !gst_element_link(data->convert, data->sink)) {
		GPlayerDEBUG("Elements could not be linked.\n");
		gst_object_unref(data->pipeline);
		return;
	}

	g_object_get(data->pipeline, "flags", &flags, NULL);
	flags &= ~GST_PLAY_FLAG_TEXT;
	g_object_set(data->pipeline, "flags", flags, NULL);

	g_signal_connect(data->source, "pad-added", (GCallback) pad_added_handler,
			data);

	data->target_state = GST_STATE_READY;
	gst_element_set_state(data->pipeline, GST_STATE_READY);

	bus = gst_element_get_bus(data->pipeline);
	bus_source = gst_bus_create_watch(bus);
	g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func,
			NULL, NULL);
	g_source_attach(bus_source, data->context);
	g_source_unref(bus_source);
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
