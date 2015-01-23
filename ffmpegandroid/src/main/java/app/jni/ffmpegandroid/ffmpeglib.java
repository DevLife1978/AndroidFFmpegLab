package app.jni.ffmpegandroid;

/**
 * Created by jayjay on 15. 1. 8..
 */
public class ffmpeglib {
    static {
        System.loadLibrary("ffmpeg");
    }
    static boolean initialized = false;
    public ffmpeglib() {
        super();
        if (initialized == false) {
            initialized = true;
            initialize();
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
//    private native long media_length(String mediaPath);
    public native void ffmpeg_test(String input_path, String output_path);
}
