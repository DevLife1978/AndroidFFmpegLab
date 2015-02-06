package app.jni.ffmpegandroid;

/**
 * Created by jayjay on 15. 1. 8..
 */
public class ffmpeglib {
    static {
        System.loadLibrary("ffmpeg");
    }
    static boolean initialized = false;

    private FFmpegLibraryCallback mCallback;

    public ffmpeglib() {
        super();
        if (initialized == false) {
            initialized = true;
            initialize();
        }
    }

    public void setCallback(FFmpegLibraryCallback callback) {
        mCallback = callback;
    }

    public void fireCallback(int frame, int total_frame) {
        if(null != mCallback) {
            mCallback.onDecodedTime(frame, total_frame);
        }
    }

    public void fireFinish(int ret, String error) {
        if(null != mCallback) {
            mCallback.onFinished(ret, error);
        }
    }

    //    public void dump2log(String mediaPath) {
//        dump(mediaPath);
//    }
//
//    public long getDuration(String mediaPath) {
//        return media_length(mediaPath);
//    }
//    public int runScaleMedia(String src, String dst) {
//        return 0;
//    }
//    public int runMuxing(){
//        return 0;
//    }
//
    private native void initialize();
//    private native void dump(String mediaPath);
    public native long media_length(String mediaPath);
    public native int media_total_frame(String mediaPath);
    public native void ffmpeg_test(String input_path, String output_path);
    public native void stop();
    public interface FFmpegLibraryCallback {
        public void onDecodedTime(int frame, int total_frame);
        public void onFinished(int ret, String error);
    }
}
