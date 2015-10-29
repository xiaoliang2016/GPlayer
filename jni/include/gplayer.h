/*
 * gplayer.h
 *
 *  Created on: 11 wrz 2015
 *      Author: Krzysztof Gawrys
 */

#include <string.h>
#include <stdint.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "customdata.h"

#include "java_callbacks.h"
#include "gst_callbacks.h"

#define WORKER_TIMEOUT 250
#define BUFFER_TIME 15
#define ERROR_BUFFERING 2
#define BUFFER_SLOW 3
#define BUFFER_FAST 4

static pthread_t gst_app_thread;

/* Do not allow seeks to be performed closer than this distance. It is visually useless, and will probably
 * confuse some demuxers. */
#define SEEK_MIN_DELAY (500 * GST_MSECOND)

/* These global variables cache values which are not changing during execution */
extern jfieldID custom_data_field_id;

// internals
void buffer_size(CustomData *data, int size);
void build_pipeline(CustomData *data);
void check_initialization_complete(CustomData *data);
void execute_seek(gint64 desired_position, CustomData *data);
void print_one_tag(const GstTagList * list, const gchar * tag, CustomData *data);

gint no_buffer_fill;
gint buffer_is_slow;
gint count_buffer_fill;
gint64 last_position;
gint64 counter;
