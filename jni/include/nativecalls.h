/*
 * java_calls.h
 *
 *  Created on: 11 wrz 2015
 *      Author: Krzysztof Gawrys
 */

// java calls
static void gst_native_class_init(JNIEnv* env, jclass klass);
extern void gst_native_finalize(JNIEnv* env, jobject thiz);
extern void gst_native_init(JNIEnv* env, jobject thiz);
static void gst_native_pause(JNIEnv* env, jobject thiz);
static void gst_native_play(JNIEnv* env, jobject thiz);
static void gst_native_pause(JNIEnv* env, jobject thiz);
static void gst_native_reset(JNIEnv* env, jobject thiz);
static void gst_native_set_notifytime(JNIEnv* env, jobject thiz, int time);
static void gst_native_set_position(JNIEnv* env, jobject thiz, int milliseconds);
static void gst_native_set_uri(JNIEnv* env, jobject thiz, jstring uri, jboolean seek);
static void gst_native_set_url(JNIEnv* env, jobject thiz, jstring uri, jboolean seek);
static void gst_native_volume(JNIEnv* env, jobject thiz, float left, float right);
static gboolean gst_native_isplaying(JNIEnv* env, jobject thiz);
static void gst_native_buffer_size(JNIEnv* env, jobject thiz, int size);
static int gst_native_get_duration(JNIEnv* env, jobject thiz);
static int gst_native_get_position(JNIEnv* env, jobject thiz);
static void gst_native_network_change(JNIEnv* env, jobject thiz, jboolean fast);

void set_notifyfunction(CustomData *data);
void buffer_size(CustomData *data, int size);
void build_pipeline(CustomData *data);
void check_initialization_complete(CustomData *data);
void execute_seek(gint64 desired_position, CustomData *data);
void print_one_tag(const GstTagList * list, const gchar * tag, CustomData *data);
