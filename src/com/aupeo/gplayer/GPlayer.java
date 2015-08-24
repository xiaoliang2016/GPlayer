package com.aupeo.gplayer;

import org.freedesktop.gstreamer.GStreamer;

import android.content.Context;
import android.util.Log;

public class GPlayer {
    
    public interface OnTimeListener
    {
        void onTime(int time);
    }
    
    public interface OnGPlayerReadyListener
    {
        void onGPlayerReady();
    }
    
    public interface OnPreparedListener
    {
        void onPrepared();
    }

    public interface OnCompletionListener
    {
        void onCompletion();
    }
    
    public interface OnPlayStartedListener
    {
        void onPlayback();
    }
    
    public interface OnBufferingUpdateListener
    {
        void onBufferingUpdate(int percent);
    }
    
    public interface OnSeekCompleteListener
    {
        public void onSeekComplete();
    }
    
    public interface OnErrorListener
    {
        boolean onError(int errorCode);
    }
   
    public void setOnErrorListener(OnErrorListener listener)
    {
        mOnErrorListener = listener;
    }

    private OnErrorListener mOnErrorListener;
    
    public void setOnSeekCompleteListener(OnSeekCompleteListener listener)
    {
        mOnSeekCompleteListener = listener;
    }

    private OnSeekCompleteListener mOnSeekCompleteListener;
    
    public void setOnPlayListener(OnPlayStartedListener listener)
    {
        mOnPlayStartedListener = listener;
    }

    private OnPlayStartedListener mOnPlayStartedListener;
    
    public void setOnBufferingUpdateListener(OnBufferingUpdateListener listener)
    {
        mOnBufferingUpdateListener = listener;
    }

    private OnBufferingUpdateListener mOnBufferingUpdateListener;
    
    public void setOnCompletionListener(OnCompletionListener listener)
    {
        mOnCompletionListener = listener;
    }

    private OnCompletionListener mOnCompletionListener;
    
    public void setOnPreparedListener(OnPreparedListener listener)
    {
        mOnPreparedListener = listener;
    }
    
    private OnPreparedListener mOnPreparedListener;
    
    public void setOnTimeListener(OnTimeListener listener)
    {
        mOnTimeListener = listener;
    }
    
    private OnTimeListener mOnTimeListener;
    
    public void setOnGPlayerReadyListener(OnGPlayerReadyListener listener)
    {
        mOnGPlayerReadyListener = listener;
    }
    
    private OnGPlayerReadyListener mOnGPlayerReadyListener;
    
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
    
    private native void nativeStop();    // Set pipeline to STOPPED
    
    private native void nativeReset();
    
    private native boolean nativeIsPlaying();
    
    private native void nativeSetVolume(float left, float right);

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
    
    public void setDataSource(String uri) {
        if (uri.contains("http")) {
            nativeSetUrl(uri);
        } else {
            nativeSetUri(uri);
        }
    }
    
    public void setNotifyTime(int time) {
        nativeSetNotifyTime(time);
    }
    
    public void start() {
        nativePlay();
    }
    
    public void pause() {
        nativePause();
    }
    
    public void stop() {
        nativeStop();
    }
    
    public void seekTo(int seek) {
        nativeSetPosition(seek);
    }
    
    public boolean isPlaying() {
        return nativeIsPlaying();
    }
    
    public void release() {
        nativeFinalize();
    }
    
    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        nativeFinalize();
    }
    
    public void onError(int errorCode) {
        Log.d("GPlayer", "onError errorCode: " + errorCode);
        mOnErrorListener.onError(errorCode);
    }

    public void onTime(int time) {
        Log.d("GPlayer", "onTime: [" + time + "]");
        mOnTimeListener.onTime(time);
    }
    
    public void onPlayComplete() {
        Log.d("GPlayer", "onPlayComplete");
        mOnCompletionListener.onCompletion();
    }

    public void onGPlayerReady() {
        Log.d("GPlayer", "onGPlayerReady");
        mOnGPlayerReadyListener.onGPlayerReady();
    }

    public void onPrepared() {
        Log.d("GPlayer", "onPrepared");
        mOnPreparedListener.onPrepared();
    }

    public void onPlayStarted() {
        Log.d("GPlayer", "onPlayStarted");
        mOnPlayStartedListener.onPlayback();
    }

    public int getCurrentPosition() {
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
        nativeStop();
        nativeSetPosition(0);
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
