/*
 * customdata.h
 *
 *  Created on: 11 wrz 2015
 *      Author: Krzysztof Gawrys
 */

#include <android/log.h>

GST_DEBUG_CATEGORY_STATIC(debug_category);
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

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
	jobject app; /* Application instance, used to call its methods. A global reference is kept. */
	GstElement *pipeline; /* The running pipeline */
	GstPad *pad;
	GstPad *ghost_pad;
	GMainContext *context; /* GLib context used to run the main loop */
	GMainLoop *main_loop; /* GLib main loop */
	gboolean initialized; /* To avoid informing the UI multiple times about the initialization */
	GstState state; /* Current pipeline state */
	gint64 duration; /* Cached clip duration */
	gint64 desired_position; /* Position to seek to, once the pipeline is running */
	GstClockTime last_seek_time; /* For seeking overflow prevention (throttling) */
	gboolean is_live; /* Live streams do not use buffering */
	GstState target_state; /* Desired pipeline state, to be set once buffering is complete */
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


static inline void
GPlayerDEBUG (const char *format, ...)
{
  va_list varargs;

  va_start (varargs, format);
  __android_log_print (ANDROID_LOG_DEBUG, "gplayer", format, varargs);
  va_end (varargs);
}
