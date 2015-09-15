/*
 * nativecalls.c
 *
 *  Created on: 14 wrz 2015
 *      Author: Krzysztof Gawrys
 */

#include <jni.h>
#include <gst/gst.h>
#include <pthread.h>
#include "include/customdata.h"
#include "include/nativecalls.h"

static pthread_key_t current_jni_env;
static JavaVM *java_vm;

jmethodID gplayer_error_id;
jmethodID gplayer_notify_time_id;
jmethodID gplayer_playback_complete_id;
jmethodID gplayer_initialized_method_id;
jmethodID gplayer_prepared_method_id;
jmethodID gplayer_playback_running_id;
jmethodID gplayer_metadata_method_id;
jfieldID custom_data_field_id;

/* List of implemented native methods */
JNINativeMethod native_methods[] = { { "nativeInit", "()V", (void *) gst_native_init }, {
		"nativeFinalize", "()V", (void *) gst_native_finalize }, { "nativeSetUri",
		"(Ljava/lang/String;Z)V", (void *) gst_native_set_uri }, { "nativeSetUrl",
		"(Ljava/lang/String;Z)V", (void *) gst_native_set_url }, { "nativeSetPosition", "(I)V",
		(void*) gst_native_set_position }, { "nativeSetNotifyTime", "(I)V",
		(void*) gst_native_set_notifytime }, { "nativeGetPosition", "()I",
		(int*) gst_native_get_position }, { "nativeGetDuration", "()I",
		(int*) gst_native_get_duration }, { "nativePlay", "()V", (void *) gst_native_play }, {
		"nativePause", "()V", (void *) gst_native_pause }, { "nativeStop", "()V",
		(void *) gst_native_pause }, { "nativeClassInit", "()Z", (void *) gst_native_class_init }, {
		"nativeReset", "()V", (void *) gst_native_reset }, { "nativeIsPlaying", "()Z",
		(gboolean *) gst_native_isplaying }, { "nativeSetVolume", "(FF)V",
		(gboolean *) gst_native_volume }, { "nativeSetBufferSize", "(I)V",
		(void *) gst_native_buffer_size } };

/* Static class initializer: retrieve method and field IDs */
void gst_native_class_init(JNIEnv* env, jclass klass) {
	custom_data_field_id = (*env)->GetFieldID(env, klass, "native_custom_data", "J");
	gplayer_error_id = (*env)->GetMethodID(env, klass, "onError", "(I)V");
	gplayer_notify_time_id = (*env)->GetMethodID(env, klass, "onTime", "(I)V");
	gplayer_playback_complete_id = (*env)->GetMethodID(env, klass, "onPlayComplete", "()V");
	gplayer_playback_running_id = (*env)->GetMethodID(env, klass, "onPlayStarted", "()V");
	gplayer_initialized_method_id = (*env)->GetMethodID(env, klass, "onGPlayerReady", "()V");
	gplayer_prepared_method_id = (*env)->GetMethodID(env, klass, "onPrepared", "()V");
	gplayer_metadata_method_id = (*env)->GetMethodID(env, klass, "onMetadata",
			"(Ljava/lang/String;)V");
}

static gboolean gst_native_isplaying(JNIEnv* env, jobject thiz) {
	GstState state;
	GstState pending;
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	gst_element_get_state(data->pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
	return state == GST_STATE_PLAYING;
}

static void gst_native_volume(JNIEnv* env, jobject thiz, float left, float right) {
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	GPlayerDEBUG("Set volume to %f", (float ) ((left + right) / 2));
	g_object_set(data->pipeline, "volume", (float) ((left + right) / 2), NULL);
}

static void gst_native_buffer_size(JNIEnv* env, jobject thiz, int size) {
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	buffer_size(data, size);
}

static void gst_native_set_notifytime(JNIEnv* env, jobject thiz, int time) {
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	data->notify_time = time;
	set_notifyfunction(data);
}

static void gst_native_reset(JNIEnv* env, jobject thiz) {
	gst_native_init(env, thiz);
}

static int gst_native_get_position(JNIEnv* env, jobject thiz) {
	gint64 position;
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);

	/* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
	if (!data || !data->pipeline || data->state < GST_STATE_PAUSED)
		return 0;

	if (!gst_element_query_position(data->pipeline, GST_FORMAT_TIME, &position)) {
		position = 0;
	}

	return (int) (position / GST_MSECOND);
}

static int gst_native_get_duration(JNIEnv* env, jobject thiz) {
	gint64 duration;
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);

	/* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
	if (!data || !data->pipeline || data->state < GST_STATE_PAUSED)
		return 0;

	if (!gst_element_query_duration(data->pipeline, GST_FORMAT_TIME, &duration)) {
		duration = 0;
	}

	return (int) (duration / GST_MSECOND);
}

static void gst_native_set_uri(JNIEnv* env, jobject thiz, jstring uri, jboolean seek) {
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	build_pipeline(data);
	if (!data || !data->pipeline)
		return;
	const jbyte *char_uri = (*env)->GetStringUTFChars(env, uri, NULL);
	gchar *url = gst_filename_to_uri(char_uri, NULL);
	GPlayerDEBUG("Setting URI to %s", url);
	if (data->target_state >= GST_STATE_READY)
		gst_element_set_state(data->pipeline, GST_STATE_READY);
	g_object_set(data->source, "uri", url, NULL);
	(*env)->ReleaseStringUTFChars(env, uri, char_uri);
	data->duration = GST_CLOCK_TIME_NONE;
	data->buffering_level = 100;
	data->allow_seek = seek;
	data->is_live = (gst_element_set_state(data->pipeline, data->target_state)
			== GST_STATE_CHANGE_NO_PREROLL);
	gplayer_prepare_complete(data);
	set_notifyfunction(data);
}

static void gst_native_set_url(JNIEnv* env, jobject thiz, jstring uri, jboolean seek) {
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	build_pipeline(data);
	if (!data || !data->pipeline)
		return;
	const jbyte *char_uri = (*env)->GetStringUTFChars(env, uri, NULL);
	GPlayerDEBUG("Setting URL to %s", char_uri);
	if (data->target_state >= GST_STATE_READY)
		gst_element_set_state(data->pipeline, GST_STATE_READY);
	g_object_set(data->source, "uri", char_uri, NULL);
	(*env)->ReleaseStringUTFChars(env, uri, char_uri);
	data->duration = GST_CLOCK_TIME_NONE;
	data->buffering_level = 100;
	data->allow_seek = seek;
	data->is_live = (gst_element_set_state(data->pipeline, data->target_state)
			== GST_STATE_CHANGE_NO_PREROLL);
	gplayer_prepare_complete(data);
	set_notifyfunction(data);
}

/* Set pipeline to PLAYING state */
static void gst_native_play(JNIEnv* env, jobject thiz) {
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	if (!data)
		return;
	GPlayerDEBUG("Setting state to PLAYING");
	data->target_state = GST_STATE_PLAYING;
	data->is_live = (gst_element_set_state(data->pipeline, GST_STATE_PLAYING)
			== GST_STATE_CHANGE_NO_PREROLL);
}

/* Set pipeline to PAUSED state */
static void gst_native_pause(JNIEnv* env, jobject thiz) {
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	if (!data)
		return;
	GPlayerDEBUG("Setting state to PAUSED");
	data->target_state = GST_STATE_PAUSED;
	data->is_live = (gst_element_set_state(data->pipeline, GST_STATE_PAUSED)
			== GST_STATE_CHANGE_NO_PREROLL);
}

/* Instruct the pipeline to seek to a different position */
static void gst_native_set_position(JNIEnv* env, jobject thiz, int milliseconds) {
	CustomData *data = GET_CUSTOM_DATA(env, thiz, custom_data_field_id);
	if (!data || !data->allow_seek || milliseconds == 0)
		return;
	gint64 desired_position = (gint64) (milliseconds * GST_MSECOND);
	if (data->state >= GST_STATE_PAUSED) {
		execute_seek(desired_position, data);
	} else {
		GPlayerDEBUG("Scheduling seek to %" GST_TIME_FORMAT " for later",
				GST_TIME_ARGS(desired_position));
		data->desired_position = desired_position;
	}
}

/* Register this thread with the VM */
JNIEnv *attach_current_thread(void) {
	JNIEnv *env;
	JavaVMAttachArgs args;

	GPlayerDEBUG("Attaching thread %p", g_thread_self());
	args.version = JNI_VERSION_1_4;
	args.name = NULL;
	args.group = NULL;

	if ((*java_vm)->AttachCurrentThread(java_vm, &env, &args) < 0) {
		GST_ERROR("Failed to attach current thread");
		return NULL;
	}

	return env;
}

/* Unregister this thread from the VM */
void detach_current_thread(void *env) {
	GPlayerDEBUG("Detaching thread %p", g_thread_self());
	(*java_vm)->DetachCurrentThread(java_vm);
}

/* Library initializer */
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
	JNIEnv *env = NULL;

	java_vm = vm;
	if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
		GPlayerDEBUG("Could not retrieve JNIEnv");
		return 0;
	}
	jclass klass = (*env)->FindClass(env, "com/aupeo/gplayer/GPlayer");
	(*env)->RegisterNatives(env, klass, native_methods, G_N_ELEMENTS(native_methods));

	pthread_key_create(&current_jni_env, detach_current_thread);

	return JNI_VERSION_1_4;
}

/* Retrieve the JNI environment for this thread */
JNIEnv *get_jni_env(void) {
	JNIEnv *env;

	if ((env = pthread_getspecific(current_jni_env)) == NULL) {
		env = attach_current_thread();
		pthread_setspecific(current_jni_env, env);
	}

	return env;
}
