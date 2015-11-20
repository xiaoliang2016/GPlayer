package com.aupeo.gplayer;

import java.io.File;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

import org.freedesktop.gstreamer.GStreamer;

import android.content.Context;
import android.os.Environment;
import android.util.Log;

public class GPlayer {

	private static final boolean LOG_FILE = false;
	public static final int ERROR_BUFFERING = 2;
	public static final int BUFFER_SLOW = 3;
	public static final int BUFFER_FAST = 4;
	public static final int UNKNOWN_ERROR = -1;
	public static final int NOT_FOUND = -2;
	public static final int NOT_SUPPORTED = -3;
	
	public interface OnTimeListener {
		void onTime(int time);
	}

	public interface OnGPlayerReadyListener {
		void onGPlayerReady();
	}

	public interface OnGPlayerMetadataListener {
		void onGPlayerMetadata(String title, String artist);
	}

	public interface OnPreparedListener {
		void onPrepared();
	}

	public interface OnCompletionListener {
		void onCompletion();
	}

	public interface OnPlayStartedListener {
		void onPlayback();
	}

	public interface OnBufferingUpdateListener {
		void onBufferingUpdate(int percent);
	}

	public interface OnSeekCompleteListener {
		public void onSeekComplete();
	}

	public interface OnErrorListener {
		boolean onError(int errorCode);
	}

	public void setOnErrorListener(OnErrorListener listener) {
		mOnErrorListener = listener;
	}

	private OnErrorListener mOnErrorListener;

	public void setOnSeekCompleteListener(OnSeekCompleteListener listener) {
		mOnSeekCompleteListener = listener;
	}

	private OnSeekCompleteListener mOnSeekCompleteListener;

	public void setOnPlayListener(OnPlayStartedListener listener) {
		mOnPlayStartedListener = listener;
	}

	private OnPlayStartedListener mOnPlayStartedListener;

	public void setOnBufferingUpdateListener(OnBufferingUpdateListener listener) {
		mOnBufferingUpdateListener = listener;
	}

	private OnBufferingUpdateListener mOnBufferingUpdateListener;

	public void setOnCompletionListener(OnCompletionListener listener) {
		mOnCompletionListener = listener;
	}

	private OnCompletionListener mOnCompletionListener;

	public void setOnPreparedListener(OnPreparedListener listener) {
		mOnPreparedListener = listener;
	}

	private OnPreparedListener mOnPreparedListener;

	public void setOnTimeListener(OnTimeListener listener) {
		mOnTimeListener = listener;
	}

	private OnTimeListener mOnTimeListener;

	public void setOnGPlayerReadyListener(OnGPlayerReadyListener listener) {
		mOnGPlayerReadyListener = listener;
	}

	private OnGPlayerReadyListener mOnGPlayerReadyListener;

	public void setOnGPlayerMetadataListener(OnGPlayerMetadataListener listener) {
		mOnGPlayerMetadataListener = listener;
	}

	private OnGPlayerMetadataListener mOnGPlayerMetadataListener;

	private native void nativeInit(); // Initialize native code, build pipeline,
										// etc

	private native void nativeFinalize(); // Destroy pipeline and shutdown
											// native code

	private native void nativeSetPosition(int milliseconds); // Seek to the
																// indicated
																// position, in
																// milliseconds

	private native void nativeSetUri(String uri, boolean seek); // Set the URI
																// of the media
																// to play

	private native void nativeSetUrl(String url, boolean seek); // Set the URI
																// of the media
																// to play

	private native void nativeSetNotifyTime(int time);

	private native int nativeGetPosition();

	private native int nativeGetDuration();

	private native void nativePlay(); // Set pipeline to PLAYING

	private native void nativePause(); // Set pipeline to PAUSED

	private native void nativeStop(); // Set pipeline to STOPPED

	private native void nativeReset();

	private native void nativeSetBufferSize(int size);

	private native boolean nativeIsPlaying();

	private native void nativeSetVolume(float left, float right);

	private native void nativeEnableLogging(boolean enable);

	private native void nativeNetworkChange(boolean fast);

	private static native boolean nativeClassInit(); // Initialize native class:
														// cache Method IDs for
														// callbacks

	private long native_custom_data; // Native code will use this to keep
										// private data

	private final Context context;

	private static GPlayer instance;

	private Process logcat_process;
	private boolean logging;

	public GPlayer(Context context) {
		this.context = context;
		instance = this;
		if (LOG_FILE) {
			SimpleDateFormat sdf = new SimpleDateFormat("yyyy_MM_dd_hh_mm_ss",
					Locale.US);
			if (isExternalStorageWritable()) {

				File appDirectory = new File(context.getCacheDir() + "/GPlayer");
				File logDirectory = new File(appDirectory + "/log");
				File logfile = new File(logDirectory, "gplayer_logcat_"
						+ sdf.format(new Date()) + ".log");

				// create app folder
				if (!appDirectory.exists()) {
					appDirectory.mkdir();
				}

				// create log folder
				if (!logDirectory.exists()) {
					logDirectory.mkdir();
				}

				try {
					Runtime.getRuntime().exec("logcat -c");
					int pid = android.os.Process.myPid();
					String[] cmd = {
							"/system/bin/sh",
							"-c",
							"logcat -v threadtime | grep " + pid + " > "
									+ logfile.toString() };

					logcat_process = Runtime.getRuntime().exec(cmd);
				} catch (IOException e) {
					Log.d("GPlayer", "GStreamer message: ", e);
				}
			}
		}

		try {
			GStreamer.init(context);
		} catch (Exception e) {
			Log.d("GPlayer", "GStreamer message: ", e);
		}
		nativeInit();
	}

	/* Checks if external storage is available for read and write */
	public boolean isExternalStorageWritable() {
		String state = Environment.getExternalStorageState();
		if (Environment.MEDIA_MOUNTED.equals(state)) {
			return true;
		}
		return false;
	}

	public void setDataSource(String uri, boolean seek) {
		if (uri.contains("mms://")) {
			onError(NOT_SUPPORTED);
		}
		if (uri.contains("http://") || uri.contains("https://")) {
			nativeSetUrl(uri, seek);
		} else {
			nativeSetUri(uri, seek);
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
		logcat_process.destroy();
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

	public void onMetadata(String string) {
		Log.d("GPlayer", "onMetadata '" + string + "'");
		if (mOnGPlayerMetadataListener != null) {
			String meta[] = string.split("\\ \\-\\ ");
			mOnGPlayerMetadataListener.onGPlayerMetadata(meta[1], meta[0]);
		} else {
			Log.d("GPlayer", "onMetadata NO mOnGPlayerMetadataListener");
		}
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
		nativeFinalize();
		nativeInit();
		nativeEnableLogging(logging);
	}

	public void setVolume(final float left, final float right) {
		Log.d("GPlayer", "setVolume " + left + "," + right);
		nativeSetVolume(left, right);
	}

	public void networkChanged(boolean fast) {
		Log.d("GPlayer", "networkChanged fast: " + fast);
		nativeNetworkChange(fast);
	}

	public void enableLogging(boolean enable) {
		logging = enable;
		nativeEnableLogging(enable);
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
