LOCAL_PATH := $(call my-dir)
LOCAL_SHORT_COMMANDS := true

# Edit this line
GSTREAMER_ROOT_ANDROID := C:\repos\Aupeo-workspace\gstreamer-libs\$(TARGET_ARCH_ABI)\gstreamer-1.0-android-1.5.90
GSTREAMER_INCLUDE_FONTS := no
GSTREAMER_INCLUDE_CA_CERTIFICATES := no
INCLUDE_COPY_FILE := no

SHELL := PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin /bin/bash

include $(CLEAR_VARS)

LOCAL_MODULE    := gplayer
LOCAL_SRC_FILES := gplayer.c java_callbacks.c nativecalls.c
LOCAL_SHARED_LIBRARIES := gstreamer_android
LOCAL_LDLIBS := -llog -landroid
include $(BUILD_SHARED_LIBRARY)



ifndef GSTREAMER_ROOT
ifndef GSTREAMER_ROOT_ANDROID
$(error GSTREAMER_ROOT_ANDROID is not defined!)
endif
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)
endif
GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_ROOT)/share/gst-android/ndk-build


GSTREAMER_PLUGINS_CODECS_RESTRICTED := mad faad
GSTREAMER_PLUGINS_CORE := coreelements app audioconvert audiorate audioresample typefindfunctions volume autodetect
GSTREAMER_PLUGINS_SYS := opensles
GSTREAMER_PLUGINS_PLAYBACK := playback
GSTREAMER_PLUGINS_EFFECTS := audiofx interleave level multifile replaygain autoconvert inter liveadder rawparse segmentclip speed audiomixer
GSTREAMER_PLUGINS_CODECS := subparse ogg vorbis apetag audioparsers auparse flv flac icydemux id3demux isomp4 multipart taglib wavparse fragmented id3tag gdp 
GSTREAMER_PLUGINS_NET := tcp rtsp rtp rtpmanager soup udp dataurisrc sdp srtp
GSTREAMER_PLUGINS         := $(GSTREAMER_PLUGINS_CORE) $(GSTREAMER_PLUGINS_PLAYBACK) $(GSTREAMER_PLUGINS_EFFECTS) $(GSTREAMER_PLUGINS_NET) $(GSTREAMER_PLUGINS_SYS) $(GSTREAMER_PLUGINS_CODECS) $(GSTREAMER_PLUGINS_CODECS_RESTRICTED)
G_IO_MODULES              := gnutls

include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer-1.0.mk
