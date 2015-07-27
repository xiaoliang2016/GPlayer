package com.aupeo.gplayer;

import org.freedesktop.gstreamer.GStreamer;

import android.content.Context;
import android.util.Log;

public class GPlayer {
    private native void nativeInit();     // Initialize native code, build pipeline, etc

    private native void nativeFinalize(); // Destroy pipeline and shutdown native code

    private native void nativeSetUri(String uri); // Set the URI of the media to play

    private native void nativePlay();     // Set pipeline to PLAYING

    private native void nativePause();    // Set pipeline to PAUSED

    private static native boolean nativeClassInit(); // Initialize native class: cache Method IDs for callbacks

    private long native_custom_data;      // Native code will use this to keep private data

    public GPlayer(Context context) {
        try {
            GStreamer.init(context);
        } catch (Exception e) {
            Log.d("GPlayer", "GStreamer message: ", e);
        }
        nativeInit();
    }
    
    public void setURI(String uri) {
        nativeSetUri(uri);
    }
    
    public void play() {
        nativePlay();
    }
    
    public void pause() {
        nativePause();
    }
    
    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        nativeFinalize();
    }
    
    void setMessage(String message) {
        Log.d("GPlayer", "GStreamer message: " + message);
    }
    
    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("gplayer");
        nativeClassInit();
    }
}
