package com.aupeo.gplayer;

import org.freedesktop.gstreamer.GStreamer;

import android.content.Context;
import android.util.Log;

public class GPlayer {
    
    public interface Listener{
        public void onError(int errorCode);
    }
    
    private native void nativeInit();     // Initialize native code, build pipeline, etc

    private native void nativeFinalize(); // Destroy pipeline and shutdown native code

    private native void nativeSetUri(String uri); // Set the URI of the media to play

    private native void nativePlay();     // Set pipeline to PLAYING

    private native void nativePause();    // Set pipeline to PAUSED

    private static native boolean nativeClassInit(); // Initialize native class: cache Method IDs for callbacks

    private long native_custom_data;      // Native code will use this to keep private data
    
    private static Listener onListener;

    public void setListener(Listener listener) {
        onListener = listener;
    }

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
    
    public void setMessage(String message) {
        Log.d("GPlayer", "GStreamer message: " + message);
    }
    
    public void onError(int errorCode) {
        Log.d("GPlayer", "OnError message: " + errorCode);
        onListener.onError(errorCode);
    }
    
    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("gplayer");
        nativeClassInit();
    }
}
