
#include <jni.h>
#include <string.h>

#include <app_jni_ffmpegandroid_ffmpeglib.h>

#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <android/log.h>

#include <android/bitmap.h>
//#include "transcoding.h"
#include <errno.h>
#include <libavutil/avassert.h>
#include "log.h"
#include <sys/stat.h>
#include "remuxing.h"
#include "muxing.h"
#include "transcoding.h"
#include "demuxing.h"

void audio_decode(const char *output, const char *input, AVCodecContext *ctx);
void video_decode(const char *outfilename, const char *filename);
static int decode_write_frame(const char *outfilename, AVCodecContext *avctx,
                              AVFrame *frame, int *frame_count, AVPacket *pkt, int last);

static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename);

static void av_dump_codec(AVCodecContext *ctx) {
    LOGI("<----- DUMP CODEC ------>");
    LOGI("Media type %d", ctx->codec_type);
    LOGI("Codec id %d", ctx->codec_id);
    LOGI("Codec tag %s", (char *)&(ctx->codec_tag));
    LOGI("Codec stream tag %s", (char *)&(ctx->stream_codec_tag));
    LOGI("bitrate %d", ctx->bit_rate);
    LOGI("bitrate tolerance %d", ctx->bit_rate_tolerance);
    LOGI("global quality %d", ctx->global_quality);
    LOGI("compression level %d", ctx->compression_level);
    LOGI("pixel format %d", ctx->pix_fmt);
}

JNIEXPORT void JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_dump(JNIEnv *env, jobject obj, jstring string)
{
    const char * media_path = (*env)->GetStringUTFChars(env, string, 0);
    return;
}

JNIEXPORT void JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_initialize
  (JNIEnv *env, jobject obj) {
    avcodec_register_all();
    av_register_all();
  }

JNIEXPORT jlong JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_media_1length(JNIEnv *env, jobject obj, jstring string)
{
    const char * media_path = (*env)->GetStringUTFChars(env, string, 0);
//    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "Media path -> %s", media_path);
    AVFormatContext *media_format = NULL;
    if(avformat_open_input(&media_format, media_path, NULL, NULL)) {
        __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "input file error");
        return 0;
    }
    if(0 > avformat_find_stream_info(media_format, NULL)) {
        LOGE("Couldn't find stream info");
    }
    int64_t duration = media_format->duration;
    avformat_close_input(&media_format);
    media_format = NULL;
    return duration;
}

char get_media_type_char(enum AVMediaType type)
{
    switch (type) {
        case AVMEDIA_TYPE_VIDEO:    return 'V';
        case AVMEDIA_TYPE_AUDIO:    return 'A';
        case AVMEDIA_TYPE_DATA:     return 'D';
        case AVMEDIA_TYPE_SUBTITLE: return 'S';
        case AVMEDIA_TYPE_ATTACHMENT:return 'T';
        default:                    return '?';
    }
}

const AVCodec *next_codec_for_id(enum AVCodecID id, const AVCodec *prev,
                                        int encoder)
{
    while ((prev = av_codec_next(prev))) {
        if (prev->id == id &&
            (encoder ? av_codec_is_encoder(prev) : av_codec_is_decoder(prev)))
            return prev;
    }
    return NULL;
}

void print_codecs_for_id(enum AVCodecID id, int encoder)
{
    const AVCodec *codec = NULL;

    char *desc;
    sprintf(desc," (%s: ", encoder ? "encoders" : "decoders");
    LOGI("CODEC %s", desc);

    while ((codec = next_codec_for_id(id, codec, encoder)))
        LOGI("%s ", codec->name);

    LOGI(")");
}

int compare_codec_desc(const void *a, const void *b)
{
    const AVCodecDescriptor * const *da = a;
    const AVCodecDescriptor * const *db = b;

    return (*da)->type != (*db)->type ? (*da)->type - (*db)->type :
           strcmp((*da)->name, (*db)->name);
}

unsigned get_codecs_sorted(const AVCodecDescriptor ***rcodecs)
{
    const AVCodecDescriptor *desc = NULL;
    const AVCodecDescriptor **codecs;
    unsigned nb_codecs = 0, i = 0;

    while ((desc = avcodec_descriptor_next(desc)))
        nb_codecs++;
    if (!(codecs = av_calloc(nb_codecs, sizeof(*codecs)))) {
        av_log(NULL, AV_LOG_ERROR, "Out of memory\n");
        exit(1);
    }
    desc = NULL;
    while ((desc = avcodec_descriptor_next(desc)))
        codecs[i++] = desc;
    av_assert0(i == nb_codecs);
    qsort(codecs, nb_codecs, sizeof(*codecs), compare_codec_desc);
    *rcodecs = codecs;
    return nb_codecs;
}

int show_codecs()
{
    const AVCodecDescriptor **codecs;
    unsigned i, nb_codecs = get_codecs_sorted(&codecs);

    char codec_info[400];
    sprintf(codec_info,"\nCodecs:\n"
           " D..... = Decoding supported\n"
           " .E.... = Encoding supported\n"
           " ..V... = Video codec\n"
           " ..A... = Audio codec\n"
           " ..S... = Subtitle codec\n"
           " ...I.. = Intra frame-only codec\n"
           " ....L. = Lossy compression\n"
           " .....S = Lossless compression\n"
           " -------\n");
    LOGI("%s", codec_info);
    for (i = 0; i < nb_codecs; i++) {
        const AVCodecDescriptor *desc = codecs[i];
        const AVCodec *codec = NULL;

        if (strstr(desc->name, "_deprecated"))
            continue;

        char des[1000];
        sprintf(des, "%s%s%c%s%s%s %-20s %s",
        (avcodec_find_decoder(desc->id) ? "D" : "."),
        (avcodec_find_encoder(desc->id) ? "E" : "."),
        (get_media_type_char(desc->type)),
        ((desc->props & AV_CODEC_PROP_INTRA_ONLY) ? "I" : "."),
        ((desc->props & AV_CODEC_PROP_LOSSY)      ? "L" : "."),
        ((desc->props & AV_CODEC_PROP_LOSSLESS)   ? "S" : "."),
        desc->name, (desc->long_name ? desc->long_name : ""));

        LOGI("%s",des);

        /* print decoders/encoders when there's more than one or their
         * names are different from codec name */
//        while ((codec = next_codec_for_id(desc->id, codec, 0))) {
//            if (strcmp(codec->name, desc->name)) {
//                print_codecs_for_id(desc->id, 0);
//                break;
//            }
//        }
//        codec = NULL;
//        while ((codec = next_codec_for_id(desc->id, codec, 1))) {
//            if (strcmp(codec->name, desc->name)) {
//                print_codecs_for_id(desc->id, 1);
//                break;
//            }
//        }

    }
    av_free(codecs);
    return 0;
}

void save_frame(AVFrame *frame, int width, int height, int iFrame) {
//    File *file;
//    char filename[32];
//    int y;
//
//    sprintf(filename, "frame%d.")
}

int extract_thumbnail(const char * video_path, const char * thumbnail_output_path) {
    avcodec_register_all();
    av_register_all();

    AVFormatContext *input_format_context = NULL, *output_format_context = NULL;
    AVInputFormat *input_format = NULL;
    AVOutputFormat *output_format = NULL;
    AVStream *input_stream = NULL;
    int input_stream_index = 0;
    AVStream *output_stream = NULL;
    AVCodecContext *input_codec_ctx = NULL;
    AVCodecContext *output_codec_ctx = NULL;
    AVCodec *input_codec = NULL;
    AVCodec *output_codec = NULL;


    if(0 > avformat_open_input(&input_format_context, video_path, NULL, NULL)) {
        LOGE("Couldn't open %s", video_path);

        return -1;
    }
    else {
        LOGI("Success open %s", video_path);
    }

    if(0 > avformat_find_stream_info(input_format_context, NULL)) {
        LOGE("Couldn't find input stream information");
        exit(1);
    }
    else {
        LOGI("Success find stream for %s", video_path);
        input_format = input_format_context->iformat;
        LOGI("Input format name -> %s, long name -> %s", input_format->name, input_format->long_name);
        for(int i=0 ; i < input_format_context->nb_streams ; i++) {
            if(AVMEDIA_TYPE_VIDEO == input_format_context->streams[i]->codec->codec_type) {
                input_stream_index = i;
                input_stream = input_format_context->streams[i];
                input_codec_ctx = input_stream->codec;
                break;
            }
        }
        if(!input_stream || !input_codec_ctx) {
            LOGE("Couldn't open input stream or input codec");
            exit(1);
        }
        else {
            input_codec = avcodec_find_decoder(input_codec_ctx->codec_id);
            if(!input_codec) {
                LOGE("Couldn't find input decoder");
                exit(1);
            }
            else {
                LOGI("Input codec name -> %s", input_codec->name);
            }
        }
    }

    if(0 > avcodec_open2(input_codec_ctx, input_codec, NULL)) {
        LOGE("Couldn't open codec %s", input_codec->name);
        exit(1);
    }
    else {
        LOGI("Success open codec %s", input_codec->name);
    }

    int width = 300;
    int height = 300;
    int output_pix_fmt = AV_PIX_FMT_RGBA;
    int output_codec_id = AV_CODEC_ID_PNG;

    output_codec = avcodec_find_encoder(output_codec_id);
    if(!output_codec) {
        LOGE("Couldn't open output codec for png");
        exit(1);
    }
    else {
        LOGI("Success find output encoder");
    }

    output_codec_ctx = avcodec_alloc_context3(output_codec);
    if(!output_codec_ctx) {
        LOGE("Coudln't alloc output codec context");
        exit(1);
    }
    else {
        LOGI("Success alloc output codec context");
        LOGI("ouput codec context bitrate -> %d", output_codec_ctx->bit_rate);
        LOGI("output codec name -> %s", avcodec_find_encoder(output_codec_ctx->codec_id)->name);
        LOGI("output timbase num -> %d, density -> %d", output_codec_ctx->time_base.num, output_codec_ctx->time_base.den);
    }

    output_codec_ctx->width = width;
    output_codec_ctx->height = height;
    output_codec_ctx->pix_fmt = output_pix_fmt;

    if( 0 > avcodec_open2(output_codec_ctx, output_codec, NULL)) {
        LOGE("Couldn't open output codec context");
        exit(1);
    }
    else {
        LOGI("Success open output codec -> %s", output_codec->name);
    }

    AVFrame *frame = av_frame_alloc();
    AVFrame *frame_rgb = av_frame_alloc();
    AVPacket packet;

    if(!frame || !frame_rgb) {
        LOGE("Couldn't alloc frame");
        exit(1);
    }
    else {
        LOGI("Success alloc frames");
    }

    int num_bytes = avpicture_get_size(output_pix_fmt, input_codec_ctx->width, input_codec_ctx->height);
    LOGI("Image num bytes %d", num_bytes);
    uint8_t *buffer = (uint8_t *)av_malloc(num_bytes * sizeof(uint8_t));

    int i=0;
    int frame_finished;

    struct SwsContext *img_convert_ctx = NULL;
    img_convert_ctx = sws_getContext(input_codec_ctx->width, input_codec_ctx->height, input_codec_ctx->pix_fmt, width, height, output_pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
    if(!img_convert_ctx) {
        LOGE("Couldn't create image convert context");
        exit(1);
    }

    if(0 > avpicture_fill((AVPicture *)frame_rgb, buffer, output_pix_fmt, width, height) ) {
        LOGE("Couldn't fill picture frame buffer");
        exit(1);
    }
    else {
        LOGI("Success fill picture frame buffer");
    }

    av_dict_set(&input_stream->metadata, "rotate", "180", 0);

    while(0 <= av_read_frame(input_format_context, &packet)) {
        if(packet.stream_index==input_stream_index) {
            LOGI("Decompressed %d:%d", i, avcodec_decode_video2(input_codec_ctx, frame, &frame_finished, &packet));

            LOGI("Finished %d", frame_finished);
            if(frame_finished) {
                LOGI("scaled slice height -> %d", sws_scale(img_convert_ctx, frame->data, frame->linesize, 0, input_codec_ctx->height, frame_rgb->data, frame_rgb->linesize));
                AVPacket output_pkt;
                av_init_packet(&output_pkt);
                output_pkt.data = NULL;
                output_pkt.size = 0;
                if( 0 > avcodec_encode_video2(output_codec_ctx, &output_pkt, frame_rgb, &frame_finished)) {
                    LOGE("Couldn't encode video to png %d", i);
                    exit(1);
                }
                else {
                    LOGI("check");
                    if(i < 5) {
                        LOGI("<--- Got Frames --->");
                        char filename[32];
                        sprintf(filename, "/sdcard/Download/output_%d.png", i);
                        FILE *f = fopen(filename, "wb");
                        if(!f) {
                            LOGE("Couldn't open output file -> %s", filename);
                            exit(1);
                        }
                        else {
                            LOGI("Write png file -> %s", filename);
                            fwrite(output_pkt.data, 1, output_pkt.size, f);
                            fclose(f);
                            chmod(filename, 0777);
                        }
                    }
                    i++;
                }
                av_free_packet(&output_pkt);
            }
        }
        av_free_packet(&packet);

        if(i > 5) {
            break;
        }
    }

end:
    if(0 > avcodec_close(input_codec_ctx)) {
        LOGE("Couldn't close codec %s", input_codec->name);
    }
    else {
        LOGI("Success close codec %s", input_codec->name);
        input_codec = NULL;
    }
    if(0 > avcodec_close(output_codec_ctx)) {
           LOGE("Couldn't close output codec %s", output_codec->name);
       }
       else {
           LOGI("Success close output codec %s", output_codec->name);
           input_codec = NULL;
    }
    sws_freeContext(img_convert_ctx);
    av_free(output_codec_ctx);
    av_free(buffer);
    av_frame_free(&frame);
    av_frame_free(&frame_rgb);
    avformat_close_input(&input_format_context);
}

static void log_callback(void *ptr, int level, const char *fmt, va_list vl) {
    if(level >= AV_LOG_INFO) {
        __android_log_vprint(ANDROID_LOG_INFO, "FFMPEG", fmt, vl);
    }
    else {
        __android_log_vprint(ANDROID_LOG_ERROR, "FFMPEG", fmt, vl);
    }
}

static void dump_output_format(AVOutputFormat *format, int audio_codec_id, int video_codec_id) {
    LOGI("\n\t\t\t\t\t\t< dump output format>");
    LOGI("name -> %s", format->name);
    LOGI("long name -> %s", format->long_name);
    LOGI("mime type -> %s", format->mime_type);
    LOGI("extensions -> %s", format->extensions);

    int audio_codec = 0 < audio_codec_id ? audio_codec_id : format->audio_codec;
    int video_codec = 0 < video_codec_id ? video_codec_id : format->video_codec;

    AVCodec *audio_encoder = avcodec_find_encoder(audio_codec);
    const char * audio_encoder_name = avcodec_get_name(audio_codec);
    AVCodec *video_encoder = avcodec_find_encoder(video_codec);
    const char * video_encoder_name = avcodec_get_name(video_codec);
    LOGI("%s audio encoder finding %s", audio_encoder_name, !audio_encoder ? "failed" : "succeed");
    LOGI("%s video encoder finding %s", video_encoder_name, !video_encoder ? "failed" : "succeed");

    AVCodec *subtitle_encoder = avcodec_find_encoder(format->subtitle_codec);
    const char *subtitle_encoder_name = avcodec_get_name(format->subtitle_codec);
    LOGI("%s subtitle encoder finding %s", subtitle_encoder_name, !subtitle_encoder ? "failed" : "succeed");

    AVCodecContext *audio_context = avcodec_alloc_context3(audio_encoder);
    AVCodecContext *video_context = avcodec_alloc_context3(video_encoder);

    char codec_string[1000];

    if(!audio_context) {
        LOGE("Couldn't open audio codec context");
    }
    else {
        avcodec_string(codec_string, 1000*sizeof(char), audio_context, 1);
        LOGI("%s", codec_string);
    }

    if(!video_context) {
        LOGE("Couldn't open video codec context");
    }
    else {
        avcodec_string(codec_string, 1000*sizeof(char), video_context, 1);
        LOGI("%s", codec_string);
    }

    avcodec_close(audio_context);
    avcodec_close(video_context);
}

JNIEXPORT void JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_ffmpeg_1test(JNIEnv *env, jobject obj, jstring input, jstring output) {
    const char *input_path = (*env)->GetStringUTFChars(env, input, 0);
    const char *output_path = (*env)->GetStringUTFChars(env, output, 0);

    av_log_set_callback(log_callback);

//    show_codecs();
    transcoding(input_path, output_path);

//    muxing("/sdcard/Movies/output.mp4");
//    AVOutputFormat *ofmt = av_guess_format(NULL, output_path, NULL);
//    if(!ofmt) {
//        LOGE("Couldn't guess output format");
//    }
//    else {
//        dump_output_format(ofmt, 0, 0);
//    }
}

JNIEXPORT void JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_stop(JNIEnv *env, jobject obj)
{
    encoding_stop();
}