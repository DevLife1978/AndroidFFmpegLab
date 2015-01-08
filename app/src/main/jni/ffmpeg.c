#include <jni.h>
#include <string.h>
#include <android/log.h>
#include <libavcodec/avcodec.h>
#include <app_jni_androidffmpeg_ffmpeg.h>


JNIEXPORT void JNICALL Java_app_jni_androidffmpeg_ffmpeg_ffmpeg(JNIEnv *env, jobject obj, jstring string)
{
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "ffmpeg function call");
    const char * media_path = (*env)->GetStringUTFChars(env, string, 0);
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Media path -> %s", media_path);
    return;
}