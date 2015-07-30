package com.aupeo.gplayer;

import org.freedesktop.gstreamer.GStreamer;

import android.content.Context;
import android.util.Log;

public class GPlayer {
    
    public interface Listener{
        public void onError(int errorCode);
        public void onPlayerState(GState newState);
        public void onTime(int time);
        public void onPlayComplete();
        public void onGPlayerReady();
    }
    
   
    private native void nativeInit();     // Initialize native code, build pipeline, etc

    private native void nativeFinalize(); // Destroy pipeline and shutdown native code

    private native void nativeSetPosition(int milliseconds); // Seek to the indicated position, in milliseconds

    private native void nativeSetUri(String uri); // Set the URI of the media to play

    private native void nativeSetUrl(String url); // Set the URI of the media to play
    
    private native void nativeSetNotifyTime(int time);
    
    private native int nativeGetPosition();
    
    private native int nativeGetDuration();

    private native void nativePlay();     // Set pipeline to PLAYING

    private native void nativePause();    // Set pipeline to PAUSED
    
    private native void nativeReset();
    
    private native boolean nativeIsPlaying();
    
    private native void nativeSetVolume(float left, float right);

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
        if (uri.contains("http")) {
            nativeSetUrl(uri);
        } else {
            nativeSetUri(uri);
        }
    }
    
    public void setNotifyTime(int time) {
        nativeSetNotifyTime(time);
    }
    
    public void play() {
        nativePlay();
    }
    
    public void pause() {
        nativePause();
    }
    
    public void seekTo(int seek) {
        nativeSetPosition(seek);
    }
    
    public boolean isPlaying() {
        return nativeIsPlaying();
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
        Log.d("GPlayer", "onError errorCode: " + errorCode);
        onListener.onError(errorCode);
    }

    public void onPlayerState(int newState) {
        Log.d("GPlayer", "onPlayerState newState: " + newState);
        onListener.onPlayerState(GState.values()[newState]);
    }

    public void onTime(int time) {
        Log.d("GPlayer", "onTime: [" + time + "]");
        onListener.onTime(time);
    }
    
    public void onPlayComplete() {
        Log.d("GPlayer", "onPlayComplete");
        onListener.onPlayComplete();
    }

    public void onGPlayerReady() {
        Log.d("GPlayer", "onGPlayerReady");
        onListener.onGPlayerReady();
    }

    public int getPosition() {
        int position = nativeGetPosition();
        Log.d("GPlayer", "nativeGetPosition: " + position);
        return position;
    }
    
    public int getDuration() {
        int duration = nativeGetDuration();
        Log.d("GPlayer", "nativeGetDuration: " + duration);
        return duration;
    }
    
    public void reset() {
        Log.d("GPlayer", "reset");
        nativeReset();
    }
    
    public void setVolume(final float left, final float right) {
        Log.d("GPlayer", "setVolume " + left + "," + right);
        nativeSetVolume(left, right);
    }
    
    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("gplayer");
        nativeClassInit();
    }
}
