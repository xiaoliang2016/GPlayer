/*
 * java_callbacks.c
 *
 *  Created on: 11 wrz 2015
 *      Author: Krzysztof Gawrys
 */

#include <jni.h>
#include <gst/gst.h>
#include "include/customdata.h"
#include "include/java_callbacks.h"

void gplayer_error(const gint message, CustomData *data)
{
	JNIEnv *env = get_jni_env();
	GPlayerDEBUG("Sending error code: %i", message);
	(*env)->CallVoidMethod(env, data->app, gplayer_error_id, message);
	if ((*env)->ExceptionCheck(env))
	{
		GST_ERROR("Failed to call Java method");
		(*env)->ExceptionClear(env);
	}
}

void gplayer_playback_complete(CustomData *data)
{
	JNIEnv *env = get_jni_env();
	GPlayerDEBUG("Sending Playback Complete Event");
	(*env)->CallVoidMethod(env, data->app, gplayer_playback_complete_id, NULL);
	if ((*env)->ExceptionCheck(env))
	{
		GST_ERROR("Failed to call Java method");
		(*env)->ExceptionClear(env);
	}
}

void gplayer_playback_running(CustomData *data)
{
	JNIEnv *env = get_jni_env();
	GPlayerDEBUG("Sending Playback Running Event");
	(*env)->CallVoidMethod(env, data->app, gplayer_playback_running_id, NULL);
	if ((*env)->ExceptionCheck(env))
	{
		GST_ERROR("Failed to call Java method");
		(*env)->ExceptionClear(env);
	}
}

void gplayer_prepare_complete(CustomData *data)
{
	JNIEnv *env = get_jni_env();
	GPlayerDEBUG("Sending Prepare Complete Event");
	(*env)->CallVoidMethod(env, data->app, gplayer_prepared_method_id, NULL);
	if ((*env)->ExceptionCheck(env))
	{
		GST_ERROR("Failed to call Java method");
		(*env)->ExceptionClear(env);
	}
}

void gplayer_metadata_update(CustomData *data, const gchar *metadata)
{
	JNIEnv *env = get_jni_env();
	GPlayerDEBUG("Sending Metadata Event");
	(*env)->CallVoidMethod(env, data->app, gplayer_metadata_method_id, ((*env)->NewStringUTF(env, metadata)));
	if ((*env)->ExceptionCheck(env))
	{
		GST_ERROR("Failed to call Java method");
		(*env)->ExceptionClear(env);
	}
}

void gplayer_notify_time(CustomData *data, int time)
{
	JNIEnv *env = get_jni_env();
	GPlayerDEBUG("Sending Time Event");
	(*env)->CallVoidMethod(env, data->app, gplayer_notify_time_id, time);
	if ((*env)->ExceptionCheck(env))
	{
		GST_ERROR("Failed to call Java method");
		(*env)->ExceptionClear(env);
	}
}

void gplayer_notify_init_complete(CustomData *data)
{
	JNIEnv *env = get_jni_env();
	GPlayerDEBUG("Sending Init Complete Event");
	(*env)->CallVoidMethod(env, data->app, gplayer_initialized_method_id, time);
	if ((*env)->ExceptionCheck(env))
	{
		GST_ERROR("Failed to call Java method");
		(*env)->ExceptionClear(env);
	}
}
