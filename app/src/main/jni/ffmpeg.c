#include <jni.h>
#include <string.h>
#include <android/log.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
#include <app_jni_androidffmpeg_ffmpeg.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FFMPEG" __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FFMPEG", __VA_ARGS__)

JNIEXPORT void JNICALL Java_app_jni_androidffmpeg_ffmpeg_ffmpeg(JNIEnv *env, jobject obj, jstring string)
{
#ifndef REGISTERED
    av_register_all();
    #define REGISTERED
#endif
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "ffmpeg function call");
    const char * media_path = (*env)->GetStringUTFChars(env, string, 0);
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Media path -> %s", media_path);
    av_dump(media_path);
    return;
}

void av_dump(const char* media_path) {
       AVFormatContext *fmt_ctx = NULL;
       AVDictionaryEntry *tag = NULL;
       int ret;

       if ((ret = avformat_open_input(&fmt_ctx, media_path, NULL, NULL)))
           return;

       while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
           __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "%s=%s\n", tag->key, tag->value);

       avformat_close_input(&fmt_ctx);
}