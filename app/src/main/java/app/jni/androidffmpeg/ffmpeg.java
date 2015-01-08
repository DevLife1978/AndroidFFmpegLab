package app.jni.androidffmpeg;

/**
 * Created by jayjay on 15. 1. 8..
 */
public class ffmpeg {
    static {
        System.loadLibrary("ffmpeg");
    }
    static boolean initialized = false;
    public ffmpeg() {
        super();
        if (initialized == false) {
            initialized = true;
            initialize();
        }
    }

    public void dump2log(String mediaPath) {
        dump(mediaPath);
    }

    public long getDuration(String mediaPath) {
        return media_length(mediaPath);
    }

    private native void initialize();
    private native void dump(String mediaPath);
    private native long media_length(String mediaPath);
}
