/*
 * customdata.h
 *
 *  Created on: 11 wrz 2015
 *      Author: Krzysztof Gawrys
 */

#include <android/log.h>

GST_DEBUG_CATEGORY_STATIC(debug_category);
#define GST_CAT_DEFAULT debug_category

#if GLIB_SIZEOF_VOID_P == 8
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(*env)->GetLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)data)
#else
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(jint)(*env)->GetLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)(jint)data)
#endif

typedef struct _CustomData {
	jobject app;
	GstElement *pipeline;
	GstElement *resample;
	GstPad *pad;
	GstPad *ghost_pad;
	GMainContext *context;
	GMainLoop *main_loop;
	gboolean initialized;
	GstState state;
	gint64 duration;
	gint64 desired_position;
	GstClockTime last_seek_time;
	gboolean is_live;
	GstState target_state;
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
  __android_log_vprint (ANDROID_LOG_DEBUG, "gplayer", format, varargs);
  va_end (varargs);
}
