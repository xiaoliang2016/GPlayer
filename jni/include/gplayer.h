/*
 * gplayer.h
 *
 *  Created on: 11 wrz 2015
 *      Author: Krzysztof Gawrys
 */

#include <string.h>
#include <stdint.h>
#include <gst/gst.h>

#include "customdata.h"

#include "java_callbacks.h"
#include "gst_callbacks.h"

#define SMALL_BUFFER 370000
#define DEFAULT_BUFFER 4194304
#define WORKER_TIMEOUT 250

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
gint count_buffer_fill;
gint64 last_position;
