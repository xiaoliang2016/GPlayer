#include <string.h>
#include <stdint.h>
#include <jni.h>
#include <android/log.h>
#include <gst/gst.h>
#include <pthread.h>
GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

#define GRAPH_LENGTH 80
#define DEFAULT_BUFFER 378000

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
  gint64 duration;              /* Cached clip duration */
  gint64 desired_position;      /* Position to seek to, once the pipeline is running */
  GstClockTime last_seek_time;  /* For seeking overflow prevention (throttling) */
  gboolean is_live;             /* Live streams do not use buffering */
  gboolean network_error;
  GSource *timeout_source;
  gint buffering_level;
} CustomData;

/* playbin2 flags */
typedef enum {
    GST_PLAY_FLAG_TEXT = (1 << 2),  /* We want subtitle output */
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
        GST_DEBUG("Notify time: %i", (int)(position / GST_MSECOND));
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
static void execute_seek (gint64 desired_position, CustomData *data) {
  gint64 diff;

  if (desired_position == GST_CLOCK_TIME_NONE)
    return;

  diff = gst_util_get_timestamp () - data->last_seek_time;

  if (GST_CLOCK_TIME_IS_VALID (data->last_seek_time) && diff < SEEK_MIN_DELAY) {
    /* The previous seek was too close, delay this one */
    GSource *timeout_source;

    if (data->desired_position == GST_CLOCK_TIME_NONE) {
      /* There was no previous seek scheduled. Setup a timer for some time in the future */
      timeout_source = g_timeout_source_new ((SEEK_MIN_DELAY - diff) / GST_MSECOND);
      g_source_set_callback (timeout_source, (GSourceFunc)delayed_seek_cb, data, NULL);
      g_source_attach (timeout_source, data->context);
      g_source_unref (timeout_source);
    }
    /* Update the desired seek position. If multiple petitions are received before it is time
     * to perform a seek, only the last one is remembered. */
    data->desired_position = desired_position;
    GST_DEBUG ("Throttling seek to %" GST_TIME_FORMAT ", will be in %" GST_TIME_FORMAT,
        GST_TIME_ARGS (desired_position), GST_TIME_ARGS (SEEK_MIN_DELAY - diff));
  } else {
      if (!data->is_live) {
          /* Perform the seek now */
          GST_DEBUG ("Seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS (desired_position));
          data->last_seek_time = gst_util_get_timestamp ();
          gst_element_seek_simple (data->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, desired_position);
      }
      data->desired_position = GST_CLOCK_TIME_NONE;
  }
}

/* Delayed seek callback. This gets called by the timer setup in the above function. */
static gboolean delayed_seek_cb (CustomData *data) {
  GST_DEBUG ("Doing delayed seek to %" GST_TIME_FORMAT, GST_TIME_ARGS (data->desired_position));
  execute_seek (data->desired_position, data);
  return FALSE;
}

/* Retrieve errors from the bus and show them on the UI */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
  gchar *message_string;

  gst_message_parse_error (msg, &err, &debug_info);
  //gplayer_error(err->code, data);

  GST_DEBUG ("error_cb: %s", debug_info);
  g_free (debug_info);
  g_clear_error (&err);
//  data->target_state = GST_STATE_NULL;
  data->network_error = TRUE;
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
}

/* Called when the End Of the Stream is reached. Just move to the beginning of the media and pause. */
static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  gplayer_playback_complete(data);
  data->target_state = GST_STATE_PAUSED;
  data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL);
}

/* Called when the duration of the media changes. Just mark it as unknown, so we re-query it in the next UI refresh. */
static void duration_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  data->duration = GST_CLOCK_TIME_NONE;
}


/* Called when buffering messages are received. We inform the UI about the current buffering level and
 * keep the pipeline paused until 100% buffering is reached. At that point, set the desired state. */
static void buffering_cb (GstBus *bus, GstMessage *msg, CustomData *data) {

  if (data->is_live)
    return;

  gst_message_parse_buffering (msg, &data->buffering_level);
      GST_DEBUG ("buffering: %d", data->buffering_level);
      if (data->buffering_level < 100) {
          if (!is_buffering) {
              if (data->buffering_level > 25) {
                  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
              }
              is_buffering = TRUE;
          }
      } else {
          is_buffering = FALSE;
      }
}

/* Called when the clock is lost */
static void clock_lost_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  if (data->target_state >= GST_STATE_PLAYING) {
    gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
    gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
  }
}

void buffer_size(CustomData *data, int size) {
    GST_DEBUG ("Set buffer size to %i", size);
    g_object_set (data->pipeline, "max-size-bytes", (guint)DEFAULT_BUFFER, NULL);
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
        buffer_size(data, DEFAULT_BUFFER);
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

/* Main method for the native code. This is executed on its own thread. */
static void *app_function (void *userdata) {
  JavaVMAttachArgs args;
  GstBus *bus;
  CustomData *data = (CustomData *)userdata;
  GSource *bus_source;
  GError *error = NULL;
  guint flags;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  /* Create our own GLib Main Context and make it the default one */
  data->context = g_main_context_new ();
  g_main_context_push_thread_default(data->context);

  /* Build pipeline */
  data->pipeline = gst_parse_launch("playbin", &error);
  if (error) {
    gplayer_error(error->code, data);
    g_clear_error (&error);
    return NULL;
  }

  /* Disable subtitles */
  g_object_get (data->pipeline, "flags", &flags, NULL);
  flags &= ~GST_PLAY_FLAG_TEXT;
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
  g_signal_connect (G_OBJECT (bus), "message::duration", (GCallback)duration_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::buffering", (GCallback)buffering_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::clock-lost", (GCallback)clock_lost_cb, data);
  gst_object_unref (bus);

  g_object_set (data->pipeline, "use-buffering", (gboolean)TRUE, NULL);
  g_object_set (data->pipeline, "use-rate-estimate", (gboolean)TRUE, NULL);
  g_object_set (data->pipeline, "low-percent", (gint)90, NULL);

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
  gchar *url = gst_filename_to_uri(char_uri, NULL);
  GST_DEBUG ("Setting URI to %s", url);
  if (data->target_state >= GST_STATE_READY)
    gst_element_set_state (data->pipeline, GST_STATE_READY);
  g_object_set(data->pipeline, "uri", url, NULL);
  (*env)->ReleaseStringUTFChars (env, uri, char_uri);
  data->duration = GST_CLOCK_TIME_NONE;
  data->buffering_level = 100;
  data->is_live = (gst_element_set_state (data->pipeline, data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
  gplayer_prepare_complete(data);
}

void gst_native_set_url (JNIEnv* env, jobject thiz, jstring uri) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data || !data->pipeline) return;
  const jbyte *char_uri = (*env)->GetStringUTFChars (env, uri, NULL);
  GST_DEBUG ("Setting URL to %s", char_uri);
  if (data->target_state >= GST_STATE_READY)
    gst_element_set_state (data->pipeline, GST_STATE_READY);
  g_object_set(data->pipeline, "uri", char_uri, NULL);
  (*env)->ReleaseStringUTFChars (env, uri, char_uri);
  data->duration = GST_CLOCK_TIME_NONE;
  data->buffering_level = 100;
  data->is_live = (gst_element_set_state (data->pipeline, data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
  gplayer_prepare_complete(data);
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

  return JNI_TRUE;
}

static void gst_native_set_notifytime(JNIEnv* env, jobject thiz, int time) {
    GstBus *bus;
    GSource *bus_source;

    CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);

    bus = gst_element_get_bus (data->pipeline);
    bus_source = gst_bus_create_watch (bus);
    g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
    g_source_attach (bus_source, data->context);
    g_source_unref (bus_source);

    if (data->timeout_source) {
        g_source_destroy(data->timeout_source);
    }
    /* Register a function that GLib will call 4 times per second */
    data->timeout_source = g_timeout_source_new (time);
    g_source_set_callback (data->timeout_source, (GSourceFunc)gplayer_notify_time, data, NULL);
    g_source_attach (data->timeout_source, data->context);
    g_source_unref (data->timeout_source);
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
  { "nativeSetUri", "(Ljava/lang/String;)V", (void *) gst_native_set_uri},
  { "nativeSetUrl", "(Ljava/lang/String;)V", (void *) gst_native_set_url},
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
