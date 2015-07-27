#include <string.h>
#include <stdint.h>
#include <jni.h>
#include <android/log.h>
#include <gst/gst.h>
#include <pthread.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

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
  GstElement *amplification;
  GstElement *bin;
  GstElement *convert;
  GstElement *sink;
  GstPad *pad;
  GstPad *ghost_pad;
  GMainContext *context;        /* GLib context used to run the main loop */
  GMainLoop *main_loop;         /* GLib main loop */
  gboolean initialized;         /* To avoid informing the UI multiple times about the initialization */
  GstState state;               /* Current pipeline state */
  GstState target_state;        /* Desired pipeline state, to be set once buffering is complete */
  GstClockTime last_seek_time;  /* For seeking overflow prevention (throttling) */
  gboolean is_live;             /* Live streams do not use buffering */
} CustomData;

/* playbin2 flags */
typedef enum {
  GST_PLAY_FLAG_DOWNLOAD = (1 << 7)
} GstPlayFlags;

/* These global variables cache values which are not changing during execution */
static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;
static jmethodID set_message_method_id;
static jmethodID gplayer_error_id;
static gboolean is_buffering;
GstState target_state;

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

/* Change the content of the UI's TextView */
static void set_ui_message (const gchar *message, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Setting message to: %s", message);
  jstring jmessage = (*env)->NewStringUTF(env, message);
  (*env)->CallVoidMethod (env, data->app, set_message_method_id, jmessage);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
  (*env)->DeleteLocalRef (env, jmessage);
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

static gboolean refresh_ui (CustomData *data) {
  gint64 current = -1;

  /* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
  if (!data || !data->pipeline || data->state < GST_STATE_PAUSED)
    return TRUE;

  /* Java expects these values in milliseconds, and GStreamer provides nanoseconds */
  return TRUE;
}

/* Retrieve errors from the bus and show them on the UI */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
  gchar *message_string;

  gst_message_parse_error (msg, &err, &debug_info);
  message_string = g_strdup_printf ("Error received from element %s: %s", GST_OBJECT_NAME (msg->src), err->message);
  GST_DEBUG ("Sending error code: %s", message_string);
  gplayer_error(err->code, data);
  g_clear_error (&err);
  data->target_state = GST_STATE_PAUSED;
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}

/* Called when the End Of the Stream is reached. Just move to the beginning of the media and pause. */
static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  data->target_state = GST_STATE_PAUSED;
  data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL);
}

/* Called when buffering messages are received. We inform the UI about the current buffering level and
 * keep the pipeline paused until 100% buffering is reached. At that point, set the desired state. */
static void buffering_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  gint percent;

  if (data->is_live)
    return;

  gst_message_parse_buffering (msg, &percent);
    GST_DEBUG ("buffering: %d", percent);
    if (percent < 100) {
      /* buffering busy */
      if (!is_buffering) {
    gchar * message_string = g_strdup_printf ("Buffering %d%%", percent);
    set_ui_message (message_string, data);
    g_free (message_string);
    if (percent > 0) {
      gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
    } else {
      gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
    }
    is_buffering = TRUE;
      }
    }

/*  if (percent < 100 && data->target_state >= GST_STATE_PAUSED) {
    gchar * message_string = g_strdup_printf ("Buffering %d%%", percent);
    gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
    set_ui_message (message_string, data);
    g_free (message_string);
    is_buffering = TRUE;
  } else if (data->target_state >= GST_STATE_PLAYING) {
    gchar * message_string = g_strdup_printf ("Buffering %d%%", percent);
    set_ui_message (message_string, data);
    g_free (message_string);
    gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
  } else if (data->target_state >= GST_STATE_PAUSED) {
    set_ui_message ("Buffering complete", data);
  }*/
}

/* Called when the clock is lost */
static void clock_lost_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  if (data->target_state >= GST_STATE_PLAYING) {
    gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
    gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
  }
}

/* Notify UI about pipeline state changes */
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    data->state = new_state;
    gchar *message = g_strdup_printf("State changed to %s", gst_element_state_get_name(new_state));
    set_ui_message(message, data);
    g_free (message);
  }
}

static gboolean buffer_timeout (CustomData *data)
{
  GstQuery *query;
  GstElement *pipeline = data->pipeline;
  gboolean busy;
  gint percent;
  gint64 estimated_total;
  guint64 play_left;

  query = gst_query_new_buffering (GST_FORMAT_TIME);

  if (!gst_element_query (pipeline, query))
    return TRUE;

  gst_query_parse_buffering_percent (query, &busy, &percent);
  gst_query_parse_buffering_range (query, NULL, NULL, NULL, &estimated_total);

  if (estimated_total == -1)
    estimated_total = 0;

    play_left = 0;

  g_message ("play_left %" G_GUINT64_FORMAT", estimated_total %" G_GUINT64_FORMAT
      ", percent %d", play_left, estimated_total, percent);

  /* we are buffering or the estimated download time is bigger than the
   * remaining playback time. We keep buffering. */
  is_buffering = (busy || estimated_total * 1.1 > play_left);

  if (!is_buffering)
    gst_element_set_state (pipeline, target_state);

  return is_buffering;
}

static void message_async_done_cb (GstBus *bus, GstMessage *msg, CustomData *data)
{
  GstElement *pipeline = data->pipeline;
  if (!is_buffering)
    gst_element_set_state (pipeline, target_state);
  else
     g_timeout_add (5, buffer_timeout, pipeline);
}

/* Check if all conditions are met to report GStreamer as initialized.
 * These conditions will change depending on the application */
static void check_initialization_complete (CustomData *data) {
  JNIEnv *env = get_jni_env ();
  if (!data->initialized && data->main_loop) {
    GST_DEBUG ("Initialization complete, notifying application. main_loop:%p", data->main_loop);

    data->initialized = TRUE;
  }
}

static void got_location (GstObject *gstobject, GstObject *prop_object, GParamSpec *prop, CustomData *data) {
  gchar *location;
  g_object_get (G_OBJECT (prop_object), "temp-location", &location, NULL);
  g_print ("Temporary file: %s\n", location);
  /* Uncomment this line to keep the temporary file after the program exits */
  /* g_object_set (G_OBJECT (prop_object), "temp-remove", FALSE, NULL); */
}

/* Main method for the native code. This is executed on its own thread. */
static void *app_function (void *userdata) {
  JavaVMAttachArgs args;
  GstBus *bus;
  CustomData *data = (CustomData *)userdata;
  GSource *timeout_source;
  GSource *bus_source;
  GError *error = NULL;
  guint flags;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  /* Create our own GLib Main Context and make it the default one */
  data->context = g_main_context_new ();
  g_main_context_push_thread_default(data->context);

  /* Build pipeline */
/*  data->pipeline = gst_parse_launch("playbin", &error);
  if (error) {
    gchar *message = g_strdup_printf("Unable to build pipeline: %s", error->message);
    g_clear_error (&error);
    set_ui_message(message, data);
    g_free (message);
    return NULL;
  }*/

  /* Build the pipeline */
    data->pipeline = gst_parse_launch ("playbin", NULL);

    data->amplification = gst_element_factory_make ("audiodynamic", "amplification");
    data->convert = gst_element_factory_make ("audioconvert", "convert");
    data->sink = gst_element_factory_make ("autoaudiosink", "audio_sink");
    if (!data->amplification || !data->convert || !data->sink) {
      GST_DEBUG ("Not all elements could be created.\n");
      return -1;
    }

    data->bin = gst_bin_new ("audio_sink_bin");
    gst_bin_add_many (GST_BIN (data->bin), data->amplification, data->convert, data->sink, NULL);
    gst_element_link_many (data->amplification, data->convert, data->sink, NULL);
    data->pad = gst_element_get_static_pad (data->amplification, "sink");
    data->ghost_pad = gst_ghost_pad_new ("sink", data->pad);
    gst_pad_set_active (data->ghost_pad, TRUE);
    gst_element_add_pad (data->bin, data->ghost_pad);
    gst_object_unref (data->pad);

    g_object_set (GST_OBJECT (data->pipeline), "audio-sink", data->bin, NULL);

    g_object_set (GST_OBJECT (data->amplification), "mode", "expander", NULL);

  /* Disable subtitles */
  g_object_get (data->pipeline, "flags", &flags, NULL);
  flags &= ~GST_PLAY_FLAG_DOWNLOAD;
  g_object_set (data->pipeline, "flags", flags, NULL);

  /* Set the pipeline to READY, so it can already accept a window handle, if we have one */
  data->target_state = GST_STATE_READY;
  gst_element_set_state(data->pipeline, GST_STATE_READY);

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->pipeline);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
  g_source_attach (bus_source, data->context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::buffering", (GCallback)buffering_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::clock-lost", (GCallback)clock_lost_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::async-done", (GCallback)message_async_done_cb, data);
  g_signal_connect (G_OBJECT (bus), "deep-notify::temp-location", (GCallback)got_location, data);
      gst_object_unref (bus);

  /* Register a function that GLib will call 4 times per second */
  timeout_source = g_timeout_source_new (250);
  g_source_set_callback (timeout_source, (GSourceFunc)refresh_ui, data, NULL);
  g_source_attach (timeout_source, data->context);
  g_source_unref (timeout_source);

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
void gst_native_set_uri (JNIEnv* env, jobject thiz, jstring uri) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data || !data->pipeline) return;
  const jbyte *char_uri = (*env)->GetStringUTFChars (env, uri, NULL);
  GST_DEBUG ("Setting URI to %s", char_uri);
  if (data->target_state >= GST_STATE_READY)
    gst_element_set_state (data->pipeline, GST_STATE_READY);
  g_object_set(data->pipeline, "uri", char_uri, NULL);
  (*env)->ReleaseStringUTFChars (env, uri, char_uri);
  data->is_live = (gst_element_set_state (data->pipeline, data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
}

/* Set pipeline to PLAYING state */
static void gst_native_play (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PLAYING");
/*
    g_object_set (G_OBJECT (data->amplification), "ratio", (gdouble)1.0, NULL);
*/
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

/* Static class initializer: retrieve method and field IDs */
static jboolean gst_native_class_init (JNIEnv* env, jclass klass) {
  custom_data_field_id = (*env)->GetFieldID (env, klass, "native_custom_data", "J");
  set_message_method_id = (*env)->GetMethodID (env, klass, "setMessage", "(Ljava/lang/String;)V");
  gplayer_error_id = (*env)->GetMethodID (env, klass, "onError", "(I)V");

  if (!custom_data_field_id || !set_message_method_id) {
    /* We emit this message through the Android log instead of the GStreamer log because the later
     * has not been initialized yet.
     */
    __android_log_print (ANDROID_LOG_ERROR, "gplayer", "The calling class does not implement all necessary interface methods");
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

/* List of implemented native methods */
static JNINativeMethod native_methods[] = {
  { "nativeInit", "()V", (void *) gst_native_init},
  { "nativeFinalize", "()V", (void *) gst_native_finalize},
  { "nativeSetUri", "(Ljava/lang/String;)V", (void *) gst_native_set_uri},
  { "nativePlay", "()V", (void *) gst_native_play},
  { "nativePause", "()V", (void *) gst_native_pause},
  { "nativeClassInit", "()Z", (void *) gst_native_class_init}
};

/* Library initializer */
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = NULL;

  java_vm = vm;
  setenv("GST_DEBUG", "*:1", 1);
  setenv("GST_DEBUG_NO_COLOR", "1", 1);

  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "gplayer", "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "com/aupeo/gplayer/GPlayer");
  (*env)->RegisterNatives (env, klass, native_methods, G_N_ELEMENTS(native_methods));

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
