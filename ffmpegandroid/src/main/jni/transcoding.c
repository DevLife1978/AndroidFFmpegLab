#include "transcoding.h"

typedef struct OutputStream {
    AVStream *stream;

    int64_t next_pts;
    int sample_count;

    AVFrame *frame;
    AVFrame *tmp_frame;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;

} OutputStream;


void finish(int result);
int open_input(const char* input);
int open_output(const char* output);
int add_stream(OutputStream *ost, AVFormatContext *ofc, AVCodec **codec, enum AVCodecID codec_id, AVCodecContext *icctx);

AVFormatContext *ifmtctx;
AVCodecContext *ivcctx;
AVCodecContext *iacctx;

AVFormatContext *ofmtctx;

int downstream(const char *input, const char *output) {
    int ret = 0;

    ret = open_input(input);
    if( 0 > ret ) {
        LOGE("Couldn't open input file -> %s", input);
        goto end;
    }

    ret = open_output(output);
    if( 0 > ret ) {
        LOGE("Couldn't open output context file -> %s", output);
        goto end;
    }

end:
    finish(ret);
}



void finish(int result) {

    LOGI("<--- finising --->");
    if(0 > result) {
        LOGE("Finish with error -> %s", av_err2str(result));
    }
    if(ifmtctx) {
        for(int i = 0 ; i < ifmtctx->nb_streams ; i++) {
            AVStream *stream = ifmtctx->streams[i];
            if(stream->codec) {
                int ret = avcodec_close(stream->codec);
                if(ret > 0) {
                    LOGE("Close codec with error -> %s", av_err2str(ret));
                }
            }
        }
    }
    avformat_free_context(ifmtctx);

    avformat_free_context(ofmtctx);
    LOGI("<--- finished --->");
}

int open_input(const char *input) {

    ifmtctx = NULL;

    int ret = 0;
    int i = 0;
    // open input format context
    ret = avformat_open_input(&ifmtctx, input, NULL, NULL);
    if( 0 > ret ) {
        LOGE("Couldn't open input format -> %s", input);
        goto end;
    }
    ret = avformat_find_stream_info(ifmtctx, NULL);
    if( 0 > ret ) {
        LOGE("Couldn't open input stream information");
        goto end;
    }

    LOGI("Input stream counts -> %d", ifmtctx->nb_streams);

    LOGI("<--- Open input stream codec start ---?");

    LOGI("Input format name -> %s", ifmtctx->iformat->name);

    ivcctx = NULL;
    iacctx = NULL;
    for(i = 0 ; i < ifmtctx->nb_streams ; i++) {
        AVStream *stream = ifmtctx->streams[i];
        AVCodec *codec = avcodec_find_decoder(stream->codec->codec_id);
        if(!codec) {
            avcodec_open2(stream->codec, NULL, NULL);
            if(!stream->codec->codec) {
                LOGE("%d codec is not prepared", i);
                ret = -1;
                goto end;
            }
        }
        AVCodecContext *cctx = stream->codec;
        switch(cctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                ivcctx = cctx;
            break;
            case AVMEDIA_TYPE_AUDIO:
                iacctx = cctx;
            break;
        }
    }
end:
    if( 0 <= ret ) {
        LOGI("Input prepared");
    }
    return ret;
}

int open_output(const char *output) {

    ofmtctx = NULL;

    OutputStream video_stream = {0}, audio_stream = {0};
    AVCodec *video_codec, *audio_codec;

    int ret = 0;

    ret = avformat_alloc_output_context2(&ofmtctx, NULL, NULL, output);
    if( 0 > ret ){
        LOGE("Forced create output context with mpeg");
        ret = avformat_alloc_output_context2(&ofmtctx, NULL, "mpeg", output);
        if( 0 > ret ) {
            LOGE("Couldn't create output context with mpeg");
            goto end;
        }
    }

    AVOutputFormat *ofmt = ofmtctx->oformat;

    if(!ofmt->video_codec) {
        LOGE("Couldn't get output video codec");
        ret = -1;
        goto end;
    }

    ret = add_stream(&video_stream, ofmtctx, &video_codec, ofmt->video_codec, ivcctx);
    if( 0 > ret ) {
        LOGE("Couldn't add video stream");
        goto end;
    }

    if(!ofmt->audio_codec) {
        LOGE("Couldn't get output audio codec");
        ret = -1;
        goto end;
    }

    ret = add_stream(&audio_stream, ofmtctx, &audio_codec, ofmt->audio_codec, iacctx);
    if( 0 > ret ) {
        LOGE("Couldn't add audio stream");
        goto end;
    }

end:
    if( 0 <= ret ) {
        LOGI("Output prepared");
    }
    return ret;
}

int add_stream(OutputStream *ost, AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id, AVCodecContext *icctx) {
    int ret = 0;

    AVCodecContext *cctx = NULL;

    *codec = avcodec_find_encoder(codec_id);
    if(!*codec) {
        LOGE("Couldn't find encoder for %s", avcodec_get_name(codec_id));
        ret = -1;
        goto end;
    }

    ost->stream = avformat_new_stream(oc, *codec);
    if(!ost->stream) {
        LOGE("Couldn't allocate stream for %s", avcodec_get_name(codec_id));
        ret = -1;
        goto end;
    }

    ost->stream->id = oc->nb_streams-1;
    cctx = ost->stream->codec;

    avcodec_copy_context(cctx, icctx);
    switch(cctx->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            LOGI("Add stream for %s video codec", avcodec_get_name(cctx->codec_id));
            LOGI("output bit rate -> %d", cctx->bit_rate);
            int test = 1280 * 720;
            if(test < (cctx->width * cctx->height)) {
                if( 1280 < cctx->width ) {
                    float tmp = cctx->width;
                    cctx->width = 1280;
                    cctx->height = (float)cctx->height / tmp * 1280;
                }
                else if( 720 < cctx->height ) {
                    float tmp = cctx->height;
                    cctx->height = 720;
                    cctx->width = (float)cctx->width / tmp * 720;
                }
            }
            LOGI("original dimensions(W:H) %d:%d, scaled dimensions(W:H) %d:%d", icctx->width, icctx->height, cctx->width, cctx->height);
            ost->stream->time_base = cctx->time_base;
            LOGI("timebase -> %d:%d", ost->stream->time_base.den, ost->stream->time_base.num);
            LOGI("gop_size -> %d", cctx->gop_size);
            LOGI("pixel format -> %d", cctx->pix_fmt);

        break;
        case AVMEDIA_TYPE_AUDIO:
            LOGI("audio sample format name -> %s", av_get_sample_fmt_name(cctx->sample_fmt));
            LOGI("audio sample rate -> %d", cctx->sample_rate);
            LOGI("audio channels -> %d", cctx->channels);
            ost->stream->time_base = cctx->time_base;
            LOGI("audio time base %d:%d", ost->stream->time_base.den, ost->stream->time_base.num);
        break;
    }

end:
    if(oc->oformat->flags & AVFMT_GLOBALHEADER) {
        cctx->flags != CODEC_FLAG_GLOBAL_HEADER;
    }
    return ret;
}