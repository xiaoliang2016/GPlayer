#include <string.h>
#include <stdint.h>
#include <jni.h>
#include <android/log.h>
#include <gst/gst.h>
#include <pthread.h>
GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

#define GRAPH_LENGTH 80
#define SMALL_BUFFER 370000
#define DEFAULT_BUFFER 2097152

/*
 * These macros provide a way to store the native pointer to CustomData, which might be 32 or 64 bits, into
 * a jlong, which is always 64 bits, without warnings.
 */
#if GLIB_SIZEOF_VOID_P == 8
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(*env)->GetLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)data)
#else
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(jint)(*env)->GetLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)(jint)data)
#endif

/* Do not allow seeks to be performed closer than this distance. It is visually useless, and will probably
 * confuse some demuxers. */
#define SEEK_MIN_DELAY (500 * GST_MSECOND)

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  jobject app;                  /* Application instance, used to call its methods. A global reference is kept. */
  GstElement *pipeline;         /* The running pipeline */
  GstPad *pad;
  GstPad *ghost_pad;
  GMainContext *context;        /* GLib context used to run the main loop */
  GMainLoop *main_loop;         /* GLib main loop */
  gboolean initialized;         /* To avoid informing the UI multiple times about the initialization */
  GstState state;               /* Current pipeline state */
  gint64 duration;              /* Cached clip duration */
  gint64 desired_position;      /* Position to seek to, once the pipeline is running */
  GstClockTime last_seek_time;  /* For seeking overflow prevention (throttling) */
  gboolean is_live;             /* Live streams do not use buffering */
  GstState target_state;        /* Desired pipeline state, to be set once buffering is complete */
  gboolean network_error;
  GSource *timeout_source;
  gint buffering_level;
  GstElement *source;
  GstElement *convert;
  GstElement *buffer;
  GstElement *sink;
  gboolean allow_seek;
  int notify_time;
} CustomData;

/* playbin2 flags */
typedef enum {
    GST_PLAY_FLAG_TEXT = (1 << 2),  /* We want subtitle output */
    GST_PLAY_FLAG_DOWNLOAD = (1 << 7), /* Enable progressive download (on selected formats) */
} GstPlayFlags;

/* These global variables cache values which are not changing during execution */
static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;
static jmethodID gplayer_error_id;
static jmethodID gplayer_notify_time_id;
static jmethodID gplayer_playback_complete_id;
static jmethodID gplayer_initialized_method_id;
static jmethodID gplayer_prepared_method_id;
static jmethodID gplayer_playback_running_id;
static jmethodID gplayer_metadata_method_id;
GstState target_state;

static void build_pipeline(CustomData *data);
static void set_notifyfunction(CustomData *data);

/*
 * Private methods
 */

/* Register this thread with the VM */
static JNIEnv *attach_current_thread (void) {
  JNIEnv *env;
  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", g_thread_self ());
  args.version = JNI_VERSION_1_4;
  args.name = NULL;
  args.group = NULL;

  if ((*java_vm)->AttachCurrentThread (java_vm, &env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }

  return env;
}

/* Unregister this thread from the VM */
static void detach_current_thread (void *env) {
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

/* Retrieve the JNI environment for this thread */
static JNIEnv *get_jni_env (void) {
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

/* Notify on Error */
static void gplayer_error (const gint message, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Sending error code: %i", message);
  (*env)->CallVoidMethod (env, data->app, gplayer_error_id, message);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
}

static void gplayer_playback_complete (CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Sending Playback Complete Event");
  (*env)->CallVoidMethod (env, data->app, gplayer_playback_complete_id, NULL);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
}

static void gplayer_playback_running (CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Sending Playback Running Event");
  (*env)->CallVoidMethod (env, data->app, gplayer_playback_running_id, NULL);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
}

static void gplayer_prepare_complete (CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Sending Prepare Complete Event");
  (*env)->CallVoidMethod (env, data->app, gplayer_prepared_method_id, NULL);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
}

static void gplayer_metadata_update (CustomData *data, const gchar *metadata) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Sending Prepare Complete Event");
  (*env)->CallVoidMethod (env, data->app, gplayer_metadata_method_id, ((*env)->NewStringUTF(env, metadata)));
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
}

void buffer_size(CustomData *data, int size)
{
    guint maxsizebytes;
    g_object_get(data->buffer, "max-size-bytes", &maxsizebytes, NULL);

    if (size != maxsizebytes)
    {
        GST_DEBUG("Set buffer size to %i", size);
        g_object_set(data->source, "buffer-size", (guint) size, NULL);
        g_object_set(data->source, "use-buffering", (gboolean) TRUE, NULL);
        g_object_set(data->source, "download", (gboolean) TRUE, NULL);

        g_object_set(data->buffer, "use-buffering", (gboolean) TRUE, NULL);
        g_object_set(data->buffer, "low-percent", (gint) 99, NULL);
        g_object_set(data->buffer, "use-rate-estimate", (gboolean) TRUE, NULL);
        g_object_set(data->buffer, "max-size-bytes", (guint) size, NULL);
        g_object_set(data->buffer, "max-size-time", (guint64) 20000000000, NULL);
    }
}

/* If we have pipeline and it is running, query the current position and clip duration and inform
 * the application */
static gboolean gplayer_notify_time (CustomData *data) {
  gint64 current = -1;
  gint64 position;

  /* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
  if (!data || !data->pipeline)
    return TRUE;

  /* If we didn't know it yet, query the stream duration */
  if (!GST_CLOCK_TIME_IS_VALID (data->duration)) {
    if (!gst_element_query_duration (data->pipeline, GST_FORMAT_TIME, &data->duration)) {
      data->duration = 0;
    }
  }

  if (!gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position)) {
    position = 0;
  }

    if (data->target_state >= GST_STATE_PLAYING)
    {
        JNIEnv *env = get_jni_env();
        guint maxsizebytes;
        guint currentlevelbytes;
        g_object_get(data->buffer, "max-size-bytes", &maxsizebytes, NULL);
        g_object_get(data->buffer, "current-level-bytes", &currentlevelbytes,
        NULL);
        if (!gst_element_query_duration(data->pipeline, GST_FORMAT_TIME,
            &data->duration))
        {
            data->duration = 0;
        }

        GST_DEBUG("Notify - buffer: %i, clbyte: %i, duration: %ld",
            maxsizebytes, currentlevelbytes, data->duration);

        (*env)->CallVoidMethod(env, data->app, gplayer_notify_time_id,
            (int) (position / GST_MSECOND));
        if ((*env)->ExceptionCheck(env))
        {
            GST_ERROR("Failed to call Java method");
            (*env)->ExceptionClear(env);
        }
        if (data->network_error == TRUE) {
            GST_DEBUG ("Retrying setting state to PLAYING");
            data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
        }
    }
  return TRUE;
}

/* Forward declaration for the delayed seek callback */
static gboolean delayed_seek_cb (CustomData *data);

/* Perform seek, if we are not too close to the previous seek. Otherwise, schedule the seek for
 * some time in the future. */
static void execute_seek(gint64 desired_position, CustomData *data)
{
    gint64 diff;

    if (desired_position == GST_CLOCK_TIME_NONE || !data->allow_seek)
        return;

    diff = gst_util_get_timestamp() - data->last_seek_time;

    if (!data->is_live)
    {
        /* Perform the seek now */
        GST_DEBUG("Seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS (desired_position));
        data->last_seek_time = gst_util_get_timestamp();
        gst_element_seek_simple(data->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, desired_position);
    }
    data->desired_position = GST_CLOCK_TIME_NONE;
}

/* Retrieve errors from the bus and show them on the UI */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
    GError *err;
    gchar *debug_info;
    gchar *message_string;

    gst_message_parse_error(msg, &err, &debug_info);
    //gplayer_error(err->code, data);

    GST_DEBUG("error_cb: %s", debug_info);
    g_free(debug_info);
    g_clear_error(&err);
    //data->target_state = GST_STATE_NULL;
    if (data->target_state > GST_STATE_PAUSED)
    {
        data->network_error = TRUE;
    }
}

static void print_one_tag (const GstTagList * list, const gchar * tag, CustomData *data)
{
  int i, num;

  num = gst_tag_list_get_tag_size (list, tag);
  for (i = 0; i < num; ++i) {
    const GValue *val;

    /* Note: when looking for specific tags, use the gst_tag_list_get_xyz() API,
     * we only use the GValue approach here because it is more generic */
    val = gst_tag_list_get_value_index (list, tag, i);
    if (G_VALUE_HOLDS_STRING (val)) {
      GST_DEBUG ("\t%20s : %s\n", tag, g_value_get_string (val));
      if (strcmp(tag, "title")) {
          gplayer_metadata_update(data, g_value_get_string (val));
      }
    } else if (G_VALUE_HOLDS_UINT (val)) {
      GST_DEBUG ("\t%20s : %u\n", tag, g_value_get_uint (val));
    } else if (G_VALUE_HOLDS_DOUBLE (val)) {
      GST_DEBUG ("\t%20s : %g\n", tag, g_value_get_double (val));
    } else if (G_VALUE_HOLDS_BOOLEAN (val)) {
      GST_DEBUG ("\t%20s : %s\n", tag,
          (g_value_get_boolean (val)) ? "true" : "false");
    } else if (GST_VALUE_HOLDS_BUFFER (val)) {
      GstBuffer *buf = gst_value_get_buffer (val);
      guint buffer_size = gst_buffer_get_size (buf);

      GST_DEBUG ("\t%20s : buffer of size %u\n", tag, buffer_size);
    } else if (GST_VALUE_HOLDS_DATE_TIME (val)) {
      GstDateTime *dt = g_value_get_boxed (val);
      gchar *dt_str = gst_date_time_to_iso8601_string (dt);

      GST_DEBUG ("\t%20s : %s\n", tag, dt_str);
      g_free (dt_str);
    } else {
      GST_DEBUG ("\t%20s : tag of type '%s'\n", tag, G_VALUE_TYPE_NAME (val));
    }
  }
}

static void tag_cb(GstBus *bus, GstMessage *msg, CustomData *data)
{
    GstTagList *tags = NULL;
    gst_message_parse_tag(msg, &tags);
    GST_DEBUG("Got tags from element %s:\n", GST_OBJECT_NAME(msg->src));
    gst_tag_list_foreach(tags, (GstTagForeachFunc)print_one_tag, data);
    gst_tag_list_unref(tags);
    gst_message_unref(msg);
}

/* Called when the End Of the Stream is reached. Just move to the beginning of the media and pause. */
static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
    if (!data->network_error && data->target_state >= GST_STATE_PLAYING)
    {
        data->target_state = GST_STATE_PAUSED;
        data->is_live = (gst_element_set_state(data->pipeline, GST_STATE_PAUSED)
            == GST_STATE_CHANGE_NO_PREROLL);
        gplayer_playback_complete(data);
    } else
    {
        //data->target_state = GST_STATE_NULL;
        data->is_live = (gst_element_set_state(data->pipeline, GST_STATE_NULL)
            == GST_STATE_CHANGE_NO_PREROLL);
    }
}

/* Called when the duration of the media changes. Just mark it as unknown, so we re-query it in the next UI refresh. */
static void duration_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  data->duration = GST_CLOCK_TIME_NONE;
}


/* Called when buffering messages are received. We inform the UI about the current buffering level and
 * keep the pipeline paused until 100% buffering is reached. At that point, set the desired state. */
static void buffering_cb(GstBus *bus, GstMessage *msg, CustomData *data)
{

    if (data->is_live)
        return;

    gst_message_parse_buffering(msg, &data->buffering_level);
    GST_DEBUG("buffering: %d", data->buffering_level);
    if (data->buffering_level > 50 && data->target_state >= GST_STATE_PLAYING)
    {
        buffer_size(data, DEFAULT_BUFFER);
        data->target_state = GST_STATE_PLAYING;
        data->is_live = (gst_element_set_state(data->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
    } else {
        buffer_size(data, SMALL_BUFFER);
    }
}

/* Called when the clock is lost */
static void clock_lost_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  if (data->target_state >= GST_STATE_PLAYING) {
      data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL);
      data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
  }
}

/* Notify UI about pipeline state changes */
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    data->state = new_state;

    /* The Ready to Paused state change is particularly interesting: */
    if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
      /* If there was a scheduled seek, perform it now that we have moved to the Paused state */
      if (GST_CLOCK_TIME_IS_VALID (data->desired_position))
        execute_seek (data->desired_position, data);
    }
    if (new_state == GST_STATE_PLAYING) {
        data->network_error = FALSE;
        gplayer_playback_running(data);
    }
  }
}

/* Check if all conditions are met to report GStreamer as initialized.
 * These conditions will change depending on the application */
static void check_initialization_complete (CustomData *data) {
  JNIEnv *env = get_jni_env ();
  if (!data->initialized && data->main_loop) {
    GST_DEBUG ("Initialization complete, notifying application. main_loop:%p", data->main_loop);

    (*env)->CallVoidMethod (env, data->app, gplayer_initialized_method_id);
    if ((*env)->ExceptionCheck (env)) {
      GST_ERROR ("Failed to call Java method");
      (*env)->ExceptionClear (env);
    }
    data->initialized = TRUE;
  }
}

/* This function will be called by the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
  GstPad *sink_pad = gst_element_get_static_pad (data->buffer, "sink");
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  GST_DEBUG ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

  /* If our converter is already linked, we have nothing to do here */
  if (gst_pad_is_linked (sink_pad)) {
      GST_DEBUG ("  We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_query_caps (new_pad, NULL);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);
  if (!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
      GST_DEBUG ("  It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
    goto exit;
  }

  if (gst_element_link (data->convert, data->sink)) {
      GST_DEBUG ("Elements could not be linked.\n");
  }
  /* Attempt the link */
  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
      GST_DEBUG ("  Type is '%s' but link failed.\n", new_pad_type);
  } else {
      GST_DEBUG ("  Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
}

static void build_pipeline(CustomData *data) {
    GstBus *bus;
    GSource *bus_source;
    GError *error = NULL;
    guint flags;

    if (data->initialized == TRUE) {
        gst_element_set_state (data->pipeline, GST_STATE_NULL);
        gst_object_unref (data->pipeline);
        data->pipeline = gst_pipeline_new ("test-pipeline");
    }

/*
    GstIterator *it = gst_bin_iterate_recurse(GST_BIN(data->pipeline));
    GValue elem = G_VALUE_INIT;
    while (gst_iterator_next(it, &elem) == GST_ITERATOR_OK)
    {
        GstElement *element = g_value_get_object(&elem);
        gst_bin_remove(GST_BIN(data->pipeline), element);
        GST_DEBUG("remove: %s", GST_ELEMENT_NAME(element));
        gst_object_unref(element);
        g_value_reset(&elem);
    }

    it = gst_bin_iterate_recurse(GST_BIN(data->pipeline));
    while (gst_iterator_next(it, &elem) == GST_ITERATOR_OK)
    {
        GstElement *element = g_value_get_object(&elem);
        GST_DEBUG("element: %s", GST_ELEMENT_NAME(element));
    }
*/

    /* Build pipeline */
    data->source = gst_element_factory_make ("uridecodebin", "source");
    data->buffer = gst_element_factory_make ("queue2", "buffer");
    data->convert = gst_element_factory_make ("audioconvert", "convert");
    data->sink = gst_element_factory_make ("autoaudiosink", "sink");

    if (!data->pipeline || !data->source || !data->convert || !data->buffer || !data->sink) {
      gplayer_error(-1, data);
      GST_DEBUG ("Not all elements could be created.\n");
      return;
    }

    gst_bin_add_many (GST_BIN (data->pipeline), data->source, data->convert, data->buffer, data->sink, NULL);
      if (!gst_element_link (data->buffer, data->convert) || !gst_element_link (data->convert, data->sink)) {
          GST_DEBUG ("Elements could not be linked.\n");
        gst_object_unref (data->pipeline);
        return;
      }

    g_object_get (data->pipeline, "flags", &flags, NULL);
    flags &= ~GST_PLAY_FLAG_TEXT;
    g_object_set (data->pipeline, "flags", flags, NULL);

    g_signal_connect (data->source, "pad-added", (GCallback)pad_added_handler, data);

    data->target_state = GST_STATE_READY;
    gst_element_set_state(data->pipeline, GST_STATE_READY);

    bus = gst_element_get_bus (data->pipeline);
    bus_source = gst_bus_create_watch (bus);
    g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
    g_source_attach (bus_source, data->context);
    g_source_unref (bus_source);
    g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
    g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, data);
    g_signal_connect (G_OBJECT (bus), "message::tag", (GCallback)tag_cb, data);
    g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
    g_signal_connect (G_OBJECT (bus), "message::duration", (GCallback)duration_cb, data);
    g_signal_connect (G_OBJECT (bus), "message::buffering", (GCallback)buffering_cb, data);
    g_signal_connect (G_OBJECT (bus), "message::clock-lost", (GCallback)clock_lost_cb, data);
    gst_object_unref (bus);

}

/* Main method for the native code. This is executed on its own thread. */
static void *app_function (void *userdata) {
  CustomData *data = (CustomData *)userdata;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  /* Create our own GLib Main Context and make it the default one */
  data->context = g_main_context_new ();
  g_main_context_push_thread_default(data->context);

  data->pipeline = gst_pipeline_new ("test-pipeline");

  build_pipeline(data);

  /* Create a GLib Main Loop and set it to run */
  GST_DEBUG ("Entering main loop... (CustomData:%p)", data);
  data->main_loop = g_main_loop_new (data->context, FALSE);
  check_initialization_complete (data);
  g_main_loop_run (data->main_loop);
  GST_DEBUG ("Exited main loop");
  g_main_loop_unref (data->main_loop);
  data->main_loop = NULL;

  /* Free resources */
  g_main_context_pop_thread_default(data->context);
  g_main_context_unref (data->context);
  data->target_state = GST_STATE_NULL;
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  gst_object_unref (data->pipeline);

  return NULL;
}

/*
 * Java Bindings
 */

/* Instruct the native code to create its internal data structure, pipeline and thread */
static void gst_native_init (JNIEnv* env, jobject thiz) {
  CustomData *data = g_new0 (CustomData, 1);
  data->last_seek_time = GST_CLOCK_TIME_NONE;
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, data);
  GST_DEBUG_CATEGORY_INIT (debug_category, "gplayer", 0, "Aupeo GStreamer Player");
  gst_debug_set_threshold_for_name("gplayer", GST_LEVEL_DEBUG);
  GST_DEBUG ("Created CustomData at %p", data);
  data->app = (*env)->NewGlobalRef (env, thiz);
  GST_DEBUG ("Created GlobalRef for app object at %p", data->app);
  pthread_create (&gst_app_thread, NULL, &app_function, data);
}

/* Quit the main loop, remove the native thread and free resources */
static void gst_native_finalize (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Quitting main loop...");
  g_main_loop_quit (data->main_loop);
  GST_DEBUG ("Waiting for thread to finish...");
  pthread_join (gst_app_thread, NULL);
  GST_DEBUG ("Deleting GlobalRef for app object at %p", data->app);
  (*env)->DeleteGlobalRef (env, data->app);
  GST_DEBUG ("Freeing CustomData at %p", data);
  g_free (data);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, NULL);
  GST_DEBUG ("Done finalizing");
}

/* Set playbin2's URI */
void gst_native_set_uri (JNIEnv* env, jobject thiz, jstring uri, jboolean seek) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  build_pipeline(data);
  if (!data || !data->pipeline) return;
  const jbyte *char_uri = (*env)->GetStringUTFChars (env, uri, NULL);
  gchar *url = gst_filename_to_uri(char_uri, NULL);
  GST_DEBUG ("Setting URI to %s", url);
  if (data->target_state >= GST_STATE_READY)
    gst_element_set_state (data->pipeline, GST_STATE_READY);
  g_object_set(data->source, "uri", url, NULL);
  (*env)->ReleaseStringUTFChars (env, uri, char_uri);
  data->duration = GST_CLOCK_TIME_NONE;
  data->buffering_level = 100;
  data->allow_seek = seek;
  data->is_live = (gst_element_set_state (data->pipeline, data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
  gplayer_prepare_complete(data);
  set_notifyfunction(data);
}

void gst_native_set_url (JNIEnv* env, jobject thiz, jstring uri, jboolean seek) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  build_pipeline(data);
  if (!data || !data->pipeline) return;
  const jbyte *char_uri = (*env)->GetStringUTFChars (env, uri, NULL);
  GST_DEBUG ("Setting URL to %s", char_uri);
  if (data->target_state >= GST_STATE_READY)
    gst_element_set_state (data->pipeline, GST_STATE_READY);
  g_object_set(data->source, "uri", char_uri, NULL);
  (*env)->ReleaseStringUTFChars (env, uri, char_uri);
  data->duration = GST_CLOCK_TIME_NONE;
  data->buffering_level = 100;
  data->allow_seek = seek;
  data->is_live = (gst_element_set_state (data->pipeline, data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
  gplayer_prepare_complete(data);
  set_notifyfunction(data);
}

/* Set pipeline to PLAYING state */
static void gst_native_play (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PLAYING");
  data->target_state = GST_STATE_PLAYING;
  data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);

}

/* Set pipeline to PAUSED state */
static void gst_native_pause (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PAUSED");
  data->target_state = GST_STATE_PAUSED;
  data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL);
}

/* Instruct the pipeline to seek to a different position */
void gst_native_set_position (JNIEnv* env, jobject thiz, int milliseconds) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  gint64 desired_position = (gint64)(milliseconds * GST_MSECOND);
  if (data->state >= GST_STATE_PAUSED) {
    execute_seek(desired_position, data);
  } else {
    GST_DEBUG ("Scheduling seek to %" GST_TIME_FORMAT " for later", GST_TIME_ARGS (desired_position));
    data->desired_position = desired_position;
  }
}

/* Static class initializer: retrieve method and field IDs */
static jboolean gst_native_class_init (JNIEnv* env, jclass klass) {
  custom_data_field_id = (*env)->GetFieldID (env, klass, "native_custom_data", "J");
  gplayer_error_id = (*env)->GetMethodID (env, klass, "onError", "(I)V");
  gplayer_notify_time_id = (*env)->GetMethodID (env, klass, "onTime", "(I)V");
  gplayer_playback_complete_id = (*env)->GetMethodID (env, klass, "onPlayComplete", "()V");
  gplayer_playback_running_id = (*env)->GetMethodID (env, klass, "onPlayStarted", "()V");
  gplayer_initialized_method_id = (*env)->GetMethodID (env, klass, "onGPlayerReady", "()V");
  gplayer_prepared_method_id = (*env)->GetMethodID (env, klass, "onPrepared", "()V");
  gplayer_metadata_method_id = (*env)->GetMethodID (env, klass, "onMetadata", "(Ljava/lang/String;)V");

  return JNI_TRUE;
}

static void set_notifyfunction(CustomData *data)
{
    GstBus *bus;
    GSource *bus_source;

    if (data->notify_time > 0)
    {
        bus = gst_element_get_bus(data->pipeline);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
        g_source_attach(bus_source, data->context);
        g_source_unref(bus_source);

        if (data->timeout_source)
        {
            g_source_destroy(data->timeout_source);
        }
        /* Register a function that GLib will call 4 times per second */
        data->timeout_source = g_timeout_source_new(data->notify_time);
        g_source_set_callback(data->timeout_source, (GSourceFunc) gplayer_notify_time, data, NULL);
        g_source_attach(data->timeout_source, data->context);
        g_source_unref(data->timeout_source);
    }
}

static void gst_native_set_notifytime(JNIEnv* env, jobject thiz, int time) {
    CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
    data->notify_time = time;
    set_notifyfunction(data);
}

static void gst_native_reset(JNIEnv* env, jobject thiz) {
    gst_native_init(env, thiz);
}

static int gst_native_get_position(JNIEnv* env, jobject thiz) {
    gint64 position;
    CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);

    /* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
    if (!data || !data->pipeline || data->state < GST_STATE_PAUSED)
      return 0;

    if (!gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position)) {
      position = 0;
    }

    return (int)(position / GST_MSECOND);
}

static int gst_native_get_duration(JNIEnv* env, jobject thiz) {
    gint64 duration;
    CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);

    /* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
    if (!data || !data->pipeline || data->state < GST_STATE_PAUSED)
      return 0;

    if (!gst_element_query_duration(data->pipeline, GST_FORMAT_TIME, &duration)) {
        duration = 0;
    }

    return (int)(duration / GST_MSECOND);
}

static gboolean gst_native_isplaying(JNIEnv* env, jobject thiz) {
    GstState state;
    GstState pending;
    CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
    gst_element_get_state(data->pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
    return state == GST_STATE_PLAYING;
}

static void gst_native_volume(JNIEnv* env, jobject thiz, float left, float right) {
    CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
    GST_DEBUG ("Set volume to %f", (float)((left+right)/2));
    g_object_set(data->pipeline, "volume", (float)((left+right)/2), NULL);
}

static void gst_native_buffer_size(JNIEnv* env, jobject thiz, int size) {
    CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
    buffer_size(data, size);
}

/* List of implemented native methods */
static JNINativeMethod native_methods[] = {
  { "nativeInit", "()V", (void *) gst_native_init},
  { "nativeFinalize", "()V", (void *) gst_native_finalize},
  { "nativeSetUri", "(Ljava/lang/String;Z)V", (void *) gst_native_set_uri},
  { "nativeSetUrl", "(Ljava/lang/String;Z)V", (void *) gst_native_set_url},
  { "nativeSetPosition", "(I)V", (void*) gst_native_set_position},
  { "nativeSetNotifyTime", "(I)V", (void*) gst_native_set_notifytime},
  { "nativeGetPosition", "()I", (int*) gst_native_get_position},
  { "nativeGetDuration", "()I", (int*) gst_native_get_duration},
  { "nativePlay", "()V", (void *) gst_native_play},
  { "nativePause", "()V", (void *) gst_native_pause},
  { "nativeStop", "()V", (void *) gst_native_pause},
  { "nativeClassInit", "()Z", (void *) gst_native_class_init},
  { "nativeReset", "()V", (void *) gst_native_reset},
  { "nativeIsPlaying", "()Z", (gboolean *) gst_native_isplaying},
  { "nativeSetVolume", "(FF)V", (gboolean *) gst_native_volume},
  { "nativeSetBufferSize", "(I)V", (void *) gst_native_buffer_size}
};

/* Library initializer */
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = NULL;

  java_vm = vm;
  setenv("GST_DEBUG", "*:1", 1);
  setenv("GST_DEBUG_NO_COLOR", "0", 1);

  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "gplayer", "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "com/aupeo/gplayer/GPlayer");
  (*env)->RegisterNatives (env, klass, native_methods, G_N_ELEMENTS(native_methods));

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
