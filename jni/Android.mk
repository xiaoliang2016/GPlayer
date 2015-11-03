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


GSTREAMER_PLUGINS_CODECS_RESTRICTED := mad
GSTREAMER_PLUGINS_CORE := coreelements adder app audioconvert audiorate audioresample audiotestsrc gio pango typefindfunctions volume autodetect
GSTREAMER_PLUGINS_SYS := opensles
GSTREAMER_PLUGINS_PLAYBACK := playback
GSTREAMER_PLUGINS_EFFECTS := audiofx cairo cutter debug deinterlace dtmf effectv equalizer gdkpixbuf imagefreeze interleave level multifile replaygain shapewipe spectrum accurip aiff audiofxbad autoconvert bayer coloreffects debugutilsbad fieldanalysis freeverb frei0r gaudieffects geometrictransform inter interlace ivtc liveadder rawparse removesilence segmentclip smooth speed audiomixer compositor
GSTREAMER_PLUGINS_CODECS := subparse ogg theora vorbis ivorbisdec alaw apetag audioparsers auparse flac flxdec icydemux id3demux isomp4 matroska mulaw multipart speex taglib vpx wavenc wavpack wavparse y4menc adpcmdec adpcmenc dashdemux fragmented id3tag kate mxf opus pcapparse pnm rfbsrc schro gstsiren smoothstreaming subenc y4mdec gdp rsvg 
GSTREAMER_PLUGINS_NET := tcp rtsp rtp rtpmanager soup udp dataurisrc sdp srtp
GSTREAMER_PLUGINS         := $(GSTREAMER_PLUGINS_CORE) $(GSTREAMER_PLUGINS_PLAYBACK) $(GSTREAMER_PLUGINS_EFFECTS) $(GSTREAMER_PLUGINS_NET) $(GSTREAMER_PLUGINS_SYS) $(GSTREAMER_PLUGINS_CODECS) $(GSTREAMER_PLUGINS_CODECS_RESTRICTED)
G_IO_MODULES              := gnutls

include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer-1.0.mk
