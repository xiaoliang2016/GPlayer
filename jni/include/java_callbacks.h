/*
 * java_callbacks.h
 *
 *  Created on: 11 wrz 2015
 *      Author: Krzysztof Gawrys
 */

extern jmethodID gplayer_error_id;
extern jmethodID gplayer_notify_time_id;
extern jmethodID gplayer_playback_complete_id;
extern jmethodID gplayer_initialized_method_id;
extern jmethodID gplayer_prepared_method_id;
extern jmethodID gplayer_playback_running_id;
extern jmethodID gplayer_metadata_method_id;
JNIEnv *get_jni_env(void);
