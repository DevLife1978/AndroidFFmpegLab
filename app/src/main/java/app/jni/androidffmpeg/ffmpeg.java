package app.jni.androidffmpeg;

/**
 * Created by jayjay on 15. 1. 8..
 */
public class ffmpeg {
    static {
        System.loadLibrary("ffmpeg");
    }

    public native void ffmpeg(String mediaPath);
}
