package com.aupeo.gplayer;

import java.io.IOException;

import org.apache.http.client.methods.HttpGet;
import org.apache.http.impl.client.DefaultHttpClient;
import org.apache.http.params.BasicHttpParams;
import org.apache.http.params.HttpConnectionParams;
import org.apache.http.params.HttpParams;
import org.freedesktop.gstreamer.GStreamer;

import android.content.Context;
import android.content.IntentFilter;
import android.os.Handler;
import android.os.Message;
import android.util.Log;

@SuppressWarnings("deprecation")
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

    protected static final int GPLAYER_NETWORK_CHANGE = 0;
   
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
    
    private native void nativeSetBufferSize(int size);
    
    private native boolean nativeIsPlaying();
    
    private native void nativeSetVolume(float left, float right);

    private static native boolean nativeClassInit(); // Initialize native class: cache Method IDs for callbacks

    private long native_custom_data;      // Native code will use this to keep private data
    
    private final Context context;
    
    public Handler handler = new Handler() {

        @Override
        public void handleMessage(Message msg) {
            if (msg.what == GPLAYER_NETWORK_CHANGE) {
                nativeSetBufferSize(64000);
                new Thread(new Runnable() {
                    
                    @SuppressWarnings("deprecation")
                    @Override
                    public void run() {
                            HttpGet httpGet = new HttpGet("http://www.google.com");
                            HttpParams httpParameters = new BasicHttpParams();
                            HttpConnectionParams.setConnectionTimeout(httpParameters, 1000);
                            HttpConnectionParams.setSoTimeout(httpParameters, 1000);

                            DefaultHttpClient httpClient = new DefaultHttpClient(httpParameters);
                            try {
                                httpClient.execute(httpGet);
                            } catch (IOException e) {
                                try {
                                    Thread.sleep(1000);
                                } catch (InterruptedException e1) {
                                    // TODO Auto-generated catch block
                                    e1.printStackTrace();
                                }
                                handler.sendEmptyMessage(GPLAYER_NETWORK_CHANGE);
                            }
                    }
                }).start();
            }
        }
        
    };

    private static GPlayer instance;
    
    private ConnectionChangeReceiver ccr = new ConnectionChangeReceiver();

    public GPlayer(Context context) {
        this.context = context;
        instance = this;
        context.registerReceiver(ccr, new IntentFilter("android.net.conn.CONNECTIVITY_CHANGE"));
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
        context.unregisterReceiver(ccr);
        nativeFinalize();
    }
    
    public void onError(int errorCode) {
        Log.d("GPlayer", "onError errorCode: " + errorCode);
        mOnErrorListener.onError(errorCode);
    }

    public void onTime(int time) {
        Log.d("GPlayer", "onTime: [" + time + "]");
        mOnTimeListener.onTime(time);
        GPlayerConnectivity.getNetworkInfo(context);
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
    
    public void networkChanged() {
        handler.sendEmptyMessage(GPLAYER_NETWORK_CHANGE);
    }
    
    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("gplayer");
        nativeClassInit();
    }

    public static GPlayer getInstance() {
        return instance;
    }    
}
