#include <jni.h>
#include <string.h>
#include <android/log.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libswscale/swscale.h>
#include <app_jni_ffmpegandroid_ffmpeg.h>
#include <android/bitmap.h>

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
            __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Codec %s %dx%d", codec->name, stream->codec->width, stream->codec->height);
        }
       }

       while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
           __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "%s=%s\n", tag->key, tag->value);

       avformat_close_input(&fmt_ctx);
}

JNIEXPORT void JNICALL Java_app_jni_ffmpegandroid_ffmpeg_dump(JNIEnv *env, jobject obj, jstring string)
{
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "ffmpeg function call");
    const char * media_path = (*env)->GetStringUTFChars(env, string, 0);
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Media path -> %s", media_path);
    av_dump(media_path);
    return;
}

JNIEXPORT void JNICALL Java_app_jni_ffmpegandroid_ffmpeg_initialize
  (JNIEnv *env, jobject obj) {
    av_register_all();
  }

JNIEXPORT jlong JNICALL Java_app_jni_ffmpegandroid_ffmpeg_media_1length(JNIEnv *env, jobject obj, jstring string)
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


static void fill_yuv_image(uint8_t *data[4], int linesize[4],
                           int width, int height, int frame_index)
{
    int x, y;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            data[0][y * linesize[0] + x] = x + y + frame_index * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            data[1][y * linesize[1] + x] = 128 + y + frame_index * 2;
            data[2][y * linesize[2] + x] = 64 + x + frame_index * 5;
        }
    }
}

int scale_video(const char *src, const char* dst)
{
    uint8_t *src_data[4], *dst_data[4];
    int src_linesize[4], dst_linesize[4];
    int src_w = 320, src_h = 240, dst_w, dst_h;
    enum AVPixelFormat src_pix_fmt = AV_PIX_FMT_YUV420P, dst_pix_fmt = AV_PIX_FMT_RGB24;
    const char *dst_size = NULL;
    const char *dst_filename = NULL;
    FILE *dst_file;
    int dst_bufsize;
    struct SwsContext *sws_ctx;
    int i, ret;


    dst_filename = dst;
    dst_size     = "320x240";

    AVFormatContext *media_format = NULL;
    if(avformat_open_input(&media_format, src, NULL, NULL)) {
        __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "input file error");
        return 0;
    }
    if( 0 > avformat_find_stream_info(media_format, NULL)) {
        __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Error while calling avformat_find_stream_info");
    }

    int64_t duration = media_format->duration;
    src_w = media_format->streams[0]->codec->width;
    src_h = media_format->streams[0]->codec->height;

    if (av_parse_video_size(&dst_w, &dst_h, dst_size) < 0) {
        LOGE(
                "Invalid size '%s', must be in the form WxH or a valid size abbreviation\n",
                dst_size);
        exit(1);
    }

    dst_file = fopen(dst_filename, "wb");
    if (!dst_file) {
        LOGE("Could not open destination file %s\n", dst_filename);
        exit(1);
    }

    /* create scaling context */
    sws_ctx = sws_getContext(src_w, src_h, src_pix_fmt,
                             dst_w, dst_h, dst_pix_fmt,
                             SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        LOGE(
                "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(src_pix_fmt), src_w, src_h,
                av_get_pix_fmt_name(dst_pix_fmt), dst_w, dst_h);
        ret = AVERROR(EINVAL);
        goto end;
    }

    /* allocate source and destination image buffers */
    if ((ret = av_image_alloc(src_data, src_linesize,
                              src_w, src_h, src_pix_fmt, 16)) < 0) {
        LOGE("Could not allocate source image\n");
        goto end;
    }

    /* buffer is going to be written to rawvideo file, no alignment */
    if ((ret = av_image_alloc(dst_data, dst_linesize,
                              dst_w, dst_h, dst_pix_fmt, 1)) < 0) {
        LOGE("Could not allocate destination image\n");
        goto end;
    }
    dst_bufsize = ret;

    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Source resolution -> %dx%d", src_w, src_h);

    for (i = 0; i < 100; i++) {
        /* generate synthetic video */
        __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Scaling %d%", i+1);
        fill_yuv_image(src_data, src_linesize, src_w, src_h, i);

        /* convert to destination format */
        sws_scale(sws_ctx, (const uint8_t * const*)src_data,
                  src_linesize, 0, src_h, dst_data, dst_linesize);

        /* write scaled image to file */
        fwrite(dst_data[0], 1, dst_bufsize, dst_file);
    }

    LOGI("Scaling succeeded. Play the output file with the command:\n"
           "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
           av_get_pix_fmt_name(dst_pix_fmt), dst_w, dst_h, dst_filename);

end:

    avformat_close_input(&media_format);
    media_format = NULL;

    fclose(dst_file);
    av_freep(&src_data[0]);
    av_freep(&dst_data[0]);
    sws_freeContext(sws_ctx);
    return ret < 0;
}

JNIEXPORT jint JNICALL Java_app_jni_ffmpegandroid_ffmpeg_scale_1media(JNIEnv *env, jobject obj, jstring src, jstring dst) {
    const char * src_path = (*env)->GetStringUTFChars(env, src, 0);
    const char * dst_path = (*env)->GetStringUTFChars(env, dst, 0);
    if(scale_video(src_path, dst_path)) {
        LOGE("Scaling failed");
    }
}