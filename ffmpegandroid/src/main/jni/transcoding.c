#include "transcoding.h"
#include <math.h>
#include <libavutil/avstring.h>
#include <libavutil/display.h>
#include <libavutil/stereo3d.h>
#include <libavutil/replaygain.h>
#include <libavutil/intreadwrite.h>
#include <app_jni_ffmpegandroid_ffmpeglib.h>

#define prepare_time() struct timespec tstart={0,0}, tend={0,0}
#define start_time() clock_gettime(CLOCK_MONOTONIC, &tstart)
#define end_time() clock_gettime(CLOCK_MONOTONIC, &tend)
#define print_time(msg) LOGI("%s duration %.5f seconds", msg, (((double)tend.tv_sec + 1.0e-9 * tend.tv_nsec)-((double)tstart.tv_sec + 1.0e-9 * tstart.tv_nsec)))

#define MIN(a, b) ((a)<(b)) ? (a) : (b);

#define checkend(value, ...) if( 0 > value ) { LOGE(__VA_ARGS__); goto end; }
#define checkendr(value, result, ...) if( 0 > value ) { LOGE(__VA_ARGS__); result; }

void (*finish)();

void set_finish(void (*finish_func)()) {
    finish = finish_func;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    LOGI("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
//    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
//    log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

static int is_finish = 0;

void encoding_stop() {
    LOGI("Pressed stop encoding");
    is_finish = 1;
}

int transcoding(const char * input, const char * output) {

    is_finish = 0;
    const char *input_path = input;
    const char *output_path = output;

    AVFormatContext *ifctx = NULL;
    AVFormatContext *ofctx = NULL;

    struct SwsContext *swsctx = NULL;

//    static uint8_t *video_dst_data[4] = {NULL};
//    static int      video_dst_linesize[4];
//    static int video_dst_bufsize;

    int ret = 0;
    int i = 0;

    ret = avformat_open_input(&ifctx, input_path, NULL, NULL);
    checkend(ret, "Couldn't open input %s", input_path);

    ret = avformat_find_stream_info(ifctx, NULL);
    checkend(ret, "Couldn't find stream info for %s", input_path);

    AVCodec *ivdecoder = NULL;
    ret = av_find_best_stream(ifctx, AVMEDIA_TYPE_VIDEO, -1, -1, &ivdecoder, 0);
    checkend(ret, "Couldn't find video stream");
    int video_stream_index = ret;

    AVCodec *iadecoder = NULL;
    ret = av_find_best_stream(ifctx, AVMEDIA_TYPE_AUDIO, -1, -1, &iadecoder, 0);
    checkend(ret, "Couldn't find audio stream");
    int audio_stream_index = ret;

    AVStream *ivstream = ifctx->streams[video_stream_index];
    AVCodecContext *ivcctx = ivstream->codec;
    LOGI("Find video stream for %s", ivdecoder->name);
    ivstream->codec->flags2 |= CODEC_FLAG2_FAST;
    av_opt_set(ivstream->codec->priv_data, "tune", "zerolatency,fastdecode", 0);
    ret = avcodec_open2(ivcctx, ivdecoder, NULL);
    checkend(ret, "Couldn't open %s video decoder", ivdecoder->name);

    AVStream *iastream = ifctx->streams[audio_stream_index];
    AVCodecContext *iacctx = iastream->codec;
    LOGI("Find audio stream for %s", iadecoder->name);

    ret = avcodec_open2(iacctx, iadecoder, NULL);
    checkend(ret, "Couldn't open %s audio decoder", iadecoder->name);

    int fps = ivstream->avg_frame_rate.den && ivstream->avg_frame_rate.num;
    int tbr = ivstream->r_frame_rate.den && ivstream->r_frame_rate.num;
    int tbn = ivstream->time_base.den && ivstream->time_base.num;
    int tbc = ivstream->codec->time_base.den && ivstream->codec->time_base.num;

    if (fps)
        LOGI("%f %s", av_q2d(ivstream->avg_frame_rate), tbr || tbn || tbc ? "fps, " : "fps");
    if (tbr)
        LOGI("%f %s", av_q2d(ivstream->r_frame_rate), tbn || tbc ? "tbr, " : "tbr");
    if (tbn)
        LOGI("%f %s", 1 / av_q2d(ivstream->time_base), tbc ? "tbn, " : "tbn");
    if (tbc)
        LOGI("%f %s", 1 / av_q2d(ivstream->codec->time_base), "tbc");

//    LOGI("Input bit rate tolerance %d", ivcctx->bit_rate_tolerance);
//    LOGI("2 %d", avio_rb32(ifctx->pb));

    AVFrame *frame = av_frame_alloc();
    AVPacket packet;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    ret = avformat_alloc_output_context2(&ofctx, NULL, NULL, output_path);
    if( 0 > ret ) {
        LOGE("Couldn't allocate output context");
        goto end;
    }

    AVOutputFormat *ofmt = ofctx->oformat;
//    ofmt->video_codec = AV_CODEC_ID_MPEG4;
    AVCodec *venc = avcodec_find_encoder(ofmt->video_codec);
    AVCodec *aenc = avcodec_find_encoder(ofmt->audio_codec);

    if(!aenc) {
        LOGE("Couldn't open audio encoder %s", avcodec_get_name(ofmt->audio_codec));
        goto end;
    }

    AVStream *ovstream = avformat_new_stream(ofctx, venc);
    if(!ovstream) {
        LOGE("Couldn't new stream output video");
        goto end;
    }
    ovstream->id = ofctx->nb_streams - 1;
    int ovstream_index = ofctx->nb_streams - 1;

    AVStream *oastream = avformat_new_stream(ofctx, aenc);
    if(!oastream) {
        LOGE("Couldn't new stream output audio");
        goto end;
    }
    oastream->id = ofctx->nb_streams - 1;
    int oastream_index = oastream->id;

    AVFrame *oframe = av_frame_alloc();
    AVCodecContext *ovcctx = ovstream->codec;

    int bypass = 0;

    int srcw = ivstream->codec->width;
    int srch = ivstream->codec->height;
    int dstw = 0;
    int dsth = 0;
//    double rotate = 0.0;
//    for( i = 0 ; i < ivstream->nb_side_data ; i++ ) {
//        AVPacketSideData sd = ivstream->side_data[i];
//        switch(sd.type) {
//            case AV_PKT_DATA_DISPLAYMATRIX:
//                rotate = av_display_rotation_get((int32_t *)sd.data);
//            break;
//        }
//    }
//    LOGI("Rotate %f", rotate);
    double rate = 1.0;
    double bit_rate = ivcctx->bit_rate;
    if(srcw > dstw || srch > dsth) {
        if(srcw > srch) {
            dstw = 1280;
            dsth = 720;
        }
        else if(srcw < srch) {
            dstw = 720;
            dsth = 1280;
        }
        else {
            dstw = 1280;
            dsth = 1280;
        }
        rate = MIN((double)dstw / (double)srcw, (double)dsth / (double)srch);
        LOGI("%f", rate);
        dstw = (double)srcw * rate;
        dsth = (double)srch * rate;
    }
    else {
        dstw = srcw;
        dsth = srch;
    }
    bit_rate = bit_rate * (double)(dstw * dsth) / (double)(srcw * srch) / 4.0;
    LOGI("Output size %d x %d", dstw, dsth);
    LOGI("Output bit rate %f", bit_rate);
    int dst_pix_fmt = ivcctx->pix_fmt;

    if(ovstream) {
        LOGI("output video codec -> %s", avcodec_get_name(ofmt->video_codec));

        ret = avcodec_get_context_defaults3(ovcctx, venc);
        if( 0 > ret) {
            goto end;
        }

//        ovcctx->bit_rate = bit_rate;
        ovcctx->width = dstw;
        ovcctx->height = dsth;
        ovcctx->bit_rate = dstw * dsth * 4;
        ovcctx->time_base = ivcctx->time_base;
        ovstream->time_base = ivstream->time_base;
        ovstream->avg_frame_rate = ivstream->avg_frame_rate;
        ovstream->r_frame_rate = ivstream->r_frame_rate;
        LOGI("q min/max %d/%d %d",ivcctx->qmin, ivcctx->qmax, ivcctx->max_qdiff);
        ovcctx->qmin = ivcctx->qmin;
        ovcctx->qmax = ivcctx->qmax;
        ovcctx->max_qdiff = ivcctx->max_qdiff;
//        ovcctx->time_base = (AVRational){1, fps};
        av_opt_set(ovcctx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(ovcctx->priv_data, "tune", "zerolatency,fastdecode", 0);
        ovcctx->flags2 |= CODEC_FLAG2_FAST;
//        ovcctx->time_base = ivcctx->time_base;
//        ovcctx->time_base.den /= 2;
//        ovstream->r_frame_rate.den /= 2;
//        ovstream->avg_frame_rate.den /= 2;
        ovcctx->gop_size = ivcctx->gop_size;
        ovcctx->pix_fmt = ivcctx->pix_fmt;
//        ovcctx->max_b_frames = 0;
        ovcctx->coder_type = FF_CODER_TYPE_VLC;

        ovcctx->profile = FF_PROFILE_H264_BASELINE;

        if(ofctx->oformat->flags & AVFMT_GLOBALHEADER) {
            ovcctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }
        ret = avcodec_open2(ovcctx, venc, NULL);
        if( 0 > ret ) {
            LOGE("Couldn't open encoder -> %s", venc->name);
            goto end;
        }
        oframe->width = ovcctx->width;
        oframe->height = ovcctx->height;
        oframe->format = ovcctx->pix_fmt;

        ret = av_frame_get_buffer(oframe, 0);
        if( 0 > ret ) {
            LOGE("Couldn't create output frame buffer");
            goto end;
        }

        AVDictionary *opt = NULL;
        av_dict_copy(&opt, ivstream->metadata, 0);
        ovstream->metadata = opt;
        opt = NULL;

        ovstream->side_data = ivstream->side_data;
    }

    AVCodecContext *oacctx = oastream->codec;
    if(oastream) {
        oacctx->sample_rate = iacctx->sample_rate;
        oacctx->channels = iacctx->channels;
        oacctx->channel_layout = av_get_default_channel_layout(oacctx->channels);
        oacctx->sample_fmt = iacctx->sample_fmt;
        oacctx->bit_rate = iacctx->bit_rate;
        oacctx->frame_size = iacctx->frame_size;

        if(ofctx->oformat->flags & AVFMT_GLOBALHEADER) {
                oacctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }
    }

    AVDictionary *opt = NULL;
    av_dict_copy(&opt, ifctx->metadata, 0);
    ofctx->metadata = opt;
    opt = NULL;

    ret = avio_open(&ofctx->pb, output_path, AVIO_FLAG_WRITE);
    if( 0 > ret ) {
        LOGE("Couldn't open write file");
        goto end;
    }

    swsctx = sws_getContext(ivcctx->width, ivcctx->height, ivcctx->pix_fmt, dstw, dsth, dst_pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);
    if(!swsctx) {
        LOGE("Couldn't get scaling context");
        goto end;
    }

    ret = av_image_alloc(oframe->data, oframe->linesize, dstw, dsth, dst_pix_fmt, 1);
    checkend(ret, "Couldn't allocate raw video buffer");

    ret = avformat_write_header(ofctx, NULL);
    if( 0 > ret ) {
        LOGE("Error occurred when opening output file: %s", av_err2str(ret));
        goto end;
    }

    int total_frame = ivstream->nb_frames;
    LOGI("Total input video frame count %" PRId64 "", ivstream->nb_frames);
    int got_frame;
    ret = av_read_frame(ifctx, &packet);
    while( 0 <= av_read_frame(ifctx, &packet)) {
        if(is_finish) {
            break;
        }
        AVPacket tmp_pkt = packet;
        do {
            if(is_finish) {
                break;
            }
            int decoded = packet.size;
            if( video_stream_index == packet.stream_index) {
                prepare_time();
                start_time();
                ret = avcodec_decode_video2(ivcctx, frame, &got_frame, &packet);
                end_time();
                print_time("Decoding");
                checkendr(ret, break, "Couldn't decode video");
                if(got_frame) {
//                    start_time();
                    int oh = sws_scale(swsctx, frame->data, frame->linesize, 0, ivcctx->height, oframe->data, oframe->linesize);
//                    end_time();
//                    print_time("Resizing");
                    int got_packet = 0;
                    AVPacket pkt = {0};
                    av_init_packet(&pkt);
                    pkt.pts = packet.pts;
                    pkt.dts = packet.dts;
                    int64_t pts = packet.pts;
                    int64_t dts = packet.dts;
                    int duration = packet.duration;
                    int convergence_duration = packet.convergence_duration;
                    if(pts != AV_NOPTS_VALUE) {
                        pts = av_rescale_q(pts, ivcctx->time_base, ovcctx->time_base);
                    }
                    if(dts != AV_NOPTS_VALUE) {
                        dts = av_rescale_q(dts, ivcctx->time_base, ovcctx->time_base);
                    }
                    if(duration > 0) {
                        duration = av_rescale_q(duration, ivcctx->time_base, ovcctx->time_base);
                    }
                    if(convergence_duration > 0) {
                        convergence_duration = av_rescale_q(convergence_duration, ivcctx->time_base, ovcctx->time_base);
                    }
                    pkt.pts = pts;
                    pkt.dts = dts;
                    pkt.duration = duration;
                    pkt.convergence_duration = convergence_duration;
                    oframe->pts = pts;
//                    start_time();
                    ret = avcodec_encode_video2(ovcctx, &pkt, oframe, &got_packet);
//                    end_time();
//                    print_time("Encoding");
                    if( 0 > ret ) {
                        LOGE("Couldn't encoding video frame: %s", av_err2str(ret));
                        av_free_packet(&pkt);
                        exit(1);
                    }
                    if(got_packet) {
                        av_copy_packet_side_data(&pkt, &packet);
                        ret = write_frame(ofctx, &ovcctx->time_base, ovstream, &pkt);
                    }
                    else {
                        ret = 0;
                    }
                    log_frame(frame->coded_picture_number, total_frame);
                    av_free_packet(&pkt);
                }

                av_frame_unref(frame);

                if( 0 > decoded ) {
                    break;
                }
                packet.data += decoded;
                packet.size -= decoded;
            }
            else if(audio_stream_index == packet.stream_index) {
                AVPacket apkt = packet;
                apkt.stream_index = oastream_index;
                ret = av_write_frame(ofctx, &apkt);
                if( 0 > ret ) {
                    LOGE("Couldn't write audio frame : %s", av_err2str(ret));
                }
                if( 0 > decoded ) {
                    break;
                }
                packet.data += decoded;
                packet.size -= decoded;
            }
        } while (0 < packet.size);
        av_free_packet(&tmp_pkt);
    }

    packet.data = NULL;
    packet.size = 0;
    av_free_packet(&packet);

    ret = av_write_trailer(ofctx);
    if( 0 > ret ) {
        LOGE("Couldn't write trailer");
    }

end:

    LOGI("Finished");
    finish();

    av_frame_free(&frame);
    av_frame_free(&oframe);
    av_free_packet(&packet);

    avcodec_close(ivcctx);
    avcodec_close(iacctx);
    avformat_close_input(&ifctx);
    avio_close(ofctx->pb);

    avformat_free_context(ofctx);
//    av_free(video_dst_data[0]);
    sws_freeContext(swsctx);
    char *error_str = "";
    if( 0 > ret ) {
        error_str = av_err2str(ret);
        LOGE("Error (%s)", av_err2str(ret));
    }
//    finish();
}