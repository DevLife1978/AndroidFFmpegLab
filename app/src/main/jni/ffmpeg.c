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

void av_dump(const char* media_path) {
       AVFormatContext *fmt_ctx = NULL;
       AVDictionaryEntry *tag = NULL;
       int ret;

       if ((ret = avformat_open_input(&fmt_ctx, media_path, NULL, NULL)))
           return;
       if( 0 > avformat_find_stream_info(fmt_ctx, NULL)) {
        __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Error while calling avformat_find_stream_info");
       }

       for( unsigned int i = 0 ; i < fmt_ctx->nb_streams ; i++) {
        AVStream *stream = fmt_ctx->streams[i];
        if(NULL != stream) {
            enum AVCodecID codecId = stream->codec->codec_id;
            AVCodec *codec = avcodec_find_decoder(codecId);
            __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Codec %s", codec->name);
        }
       }

       while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
           __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "%s=%s\n", tag->key, tag->value);

       avformat_close_input(&fmt_ctx);
}

JNIEXPORT void JNICALL Java_app_jni_androidffmpeg_ffmpeg_dump(JNIEnv *env, jobject obj, jstring string)
{
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "ffmpeg function call");
    const char * media_path = (*env)->GetStringUTFChars(env, string, 0);
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Media path -> %s", media_path);
    av_dump(media_path);
    return;
}

JNIEXPORT void JNICALL Java_app_jni_androidffmpeg_ffmpeg_initialize
  (JNIEnv *env, jobject obj) {
    av_register_all();
  }

JNIEXPORT jlong JNICALL Java_app_jni_androidffmpeg_ffmpeg_media_1length(JNIEnv *env, jobject obj, jstring string)
{
    const char * media_path = (*env)->GetStringUTFChars(env, string, 0);
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Media path -> %s", media_path);
    AVFormatContext *media_format = NULL;
    if(avformat_open_input(&media_format, media_path, NULL, NULL)) {
        __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "input file error");
        return 0;
    }
    int64_t duration = media_format->duration;
    avformat_close_input(&media_format);
    media_format = NULL;
    return duration;
}