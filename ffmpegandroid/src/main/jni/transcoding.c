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

const char *input_path = NULL;
const char *output_path = NULL;

AVFormatContext *ifctx = NULL;
AVStream *iastream = NULL;
AVCodecContext *iacctx = NULL;
AVStream *ivstream = NULL;
AVCodecContext *ivcctx = NULL;

AVFormatContext *ofctx = NULL;
AVStream *oastream = NULL;
AVCodecContext *oacctx = NULL;
AVStream *ovstream = NULL;
AVCodecContext *ovcctx = NULL;

struct SwsContext *swsctx = NULL;

int transcoding(const char * input, const char * output) {

    is_finish = 0;
    input_path = input;
    output_path = output;

    int ret = 0;
    int i = 0;
    ret = avformat_open_input(&ifctx, input_path, NULL, NULL);
    checkend(ret, "Couldn't open input %s", input_path);

    ret = avformat_find_stream_info(ifctx, NULL);
    checkend(ret, "Couldn't find stream info for %s", input_path);
    ret = avformat_alloc_output_context2(&ofctx, NULL, NULL, output_path);
    if( 0 > ret ) {
        LOGE("Couldn't allocate output context");
        goto end;
    }
    for(i = 0 ; i < ifctx->nb_streams ; i++ ){

        AVStream *stream = ifctx->streams[i];
        AVCodecContext *cctx = stream->codec;
        enum AVCodecID codec_id = cctx->codec_id;
        enum AVMediaType media_type = cctx->codec_type;
        const char * media_type_name = av_get_media_type_string(media_type);
        const char * codec_name = avcodec_get_name(codec_id);
        AVCodec *decoder = avcodec_find_decoder(codec_id);
        if(NULL == decoder) {
            LOGE("Couldn't find %s %s decoder", codec_name, media_type_name);
            ret = -1;
            goto end;
        }
        switch(media_type) {
            case AVMEDIA_TYPE_AUDIO:
            {
                iastream = stream;
                iacctx = cctx;
                ret = avcodec_open2(iacctx, decoder, NULL);
                if ( 0 > ret ) {
                    LOGE("Couldn't open %s audio decoder", decoder->name);
                    goto end;
                }

                AVCodec *aencoder = avcodec_find_encoder(ofctx->oformat->audio_codec);
                if(!aencoder) {
                    LOGE("Couldn't find %s audio encoder", avcodec_get_name(ofctx->oformat->audio_codec));
                    goto end;
                }
                oastream = avformat_new_stream(ofctx, aencoder);
                if(!oastream) {
                    LOGE("Couldn't add output audio stream");
                    ret = -1;
                    goto end;
                }
                oacctx=oastream->codec;

                oacctx->sample_rate = iacctx->sample_rate;
                oacctx->channels = iacctx->channels;
                oacctx->channel_layout = av_get_default_channel_layout(oacctx->channels);
                oacctx->sample_fmt = iacctx->sample_fmt;
                oacctx->bit_rate = iacctx->bit_rate;
                oacctx->frame_size = iacctx->frame_size;

                if(ofctx->oformat->flags & AVFMT_GLOBALHEADER) {
                        oacctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
                }
                // i passthrough audio stream to output file, audio encoder is not neccessary.
            }
            break;
            case AVMEDIA_TYPE_VIDEO:
            {
                ivstream = stream;
                ivcctx = cctx;
                ret = avcodec_open2(ivcctx, decoder, NULL);
                if( 0 > ret ) {
                    LOGE("Couldn't open %s video encoder", decoder->name);
                    goto end;
                }
                AVCodec *vencoder = avcodec_find_encoder(ofctx->oformat->video_codec);
                if(!vencoder) {
                    LOGE("Couldn't find %s video encoder", avcodec_get_name(ofctx->oformat->video_codec));
                    goto end;
                }
                ovstream = avformat_new_stream(ofctx, vencoder);
                if(!ovstream) {
                    LOGE("Couldn't add output video stream");
                    ret = -1;
                    goto end;
                }
                ovcctx = ovstream->codec;
                // calculate rescale size
                int srcw = ivcctx->width;
                int srch = ivcctx->height;
                int dstw = 1280;
                int dsth = 1280;
                double rate = 1.0;
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

                int bit_rate = dstw * dsth * 4;

                ret = avcodec_get_context_defaults3(ovcctx, vencoder);
                if( 0 > ret) {
                    goto end;
                }
                ovstream->time_base = ivstream->time_base;
                ovstream->avg_frame_rate = ivstream->avg_frame_rate;
                ovstream->r_frame_rate = ivstream->r_frame_rate;
                AVDictionary *opt = NULL;
                av_dict_copy(&opt, ivstream->metadata, 0);
                ovstream->metadata = opt;
                opt = NULL;
                ovstream->side_data = ivstream->side_data;

                ovcctx->width = dstw;
                ovcctx->height = dsth;
                ovcctx->bit_rate = bit_rate;
                ovcctx->time_base = ivcctx->time_base;
                ovcctx->qmin = 10; //ivcctx->qmin;
                ovcctx->qmax = 80; //ivcctx->qmax;
                ovcctx->max_qdiff = 4; //ivcctx->max_qdiff;
//                ovcctx->qmin = ivcctx->qmin;
//                ovcctx->qmax = ivcctx->qmax;
//                ovcctx->max_qdiff = ivcctx->max_qdiff;
                av_opt_set(ovcctx->priv_data, "preset", "ultrafast", 0);
                av_opt_set(ovcctx->priv_data, "tune", "zerolatency,fastdecode", 0);
                ovcctx->flags2 |= CODEC_FLAG2_FAST;
                ovcctx->gop_size = ivcctx->gop_size;
                ovcctx->pix_fmt = ivcctx->pix_fmt;
                ovcctx->coder_type = FF_CODER_TYPE_VLC;

                ovcctx->profile = FF_PROFILE_H264_BASELINE;

                if(ofctx->oformat->flags & AVFMT_GLOBALHEADER) {
                    ovcctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
                }
                ret = avcodec_open2(ovcctx, vencoder, NULL);
                if( 0 > ret ) {
                    LOGE("Couldn't open encoder -> %s", vencoder->name);
                    goto end;
                }
            }
            break;
        }
    }

    av_dict_copy(&ofctx->metadata, ifctx->metadata, 0);

    ret = avio_open(&ofctx->pb, output_path, AVIO_FLAG_WRITE);
    if( 0 > ret ) {
        goto end;
    }
    ret = avformat_write_header(ofctx, NULL);
    if( 0 > ret ) {
        LOGE("Error occurred when opening output file: %s", av_err2str(ret));
        goto end;
    }

    swsctx = sws_getContext(ivcctx->width, ivcctx->height, ivcctx->pix_fmt, ovcctx->width, ovcctx->height, ovcctx->pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);
    if(!swsctx) {
        LOGE("Couldn't get rscale context");
        ret = -1;
        goto end;
    }
//
    AVFrame *decframe = av_frame_alloc();
    if(!decframe) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    AVPacket decpkt = {0};
    av_init_packet(&decpkt);

    AVFrame *encframe = av_frame_alloc();
    if(!encframe) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    encframe->width = ovcctx->width;
    encframe->height = ovcctx->height;
    encframe->format = ovcctx->pix_fmt;
    ret = av_frame_get_buffer(encframe, 0);
    if( 0 > ret ) {
        LOGE("Couldn't create output frame buffer");
        goto end;
    }
    ret = av_image_alloc(encframe->data, encframe->linesize, ivcctx->width, ivcctx->height, ivcctx->pix_fmt, 1);
    if( 0 > ret ) {
        goto end;
    }
    int total_frame = ivstream->nb_frames;
    int got_frame = 0;
    while( 0 <= av_read_frame(ifctx, &decpkt)) {
        if(is_finish) {
            av_free_packet(&decpkt);
            break;
        }
        AVPacket tmp_pkt = decpkt;
        do {
            if(is_finish) {
                av_free_packet(&tmp_pkt);
                break;
            }
            int decoded = decpkt.size;
            int stream_index = decpkt.stream_index;
            if(ivstream->index == stream_index) { // video
                LOGI("Video writing");
                ret = avcodec_decode_video2(ivcctx, decframe, &got_frame, &decpkt);
                if( 0 > ret ) {
                    LOGI("Couldn't decode video");
                    goto end;
                }
                log_frame(decframe->coded_picture_number + 1, total_frame);
                if(got_frame) {
                    int sh = sws_scale(swsctx, decframe->data, decframe->linesize, 0, ivcctx->height, encframe->data, encframe->linesize);
                    AVPacket encpkt = {0};
                    av_init_packet(&encpkt);
                    int64_t pts = decpkt.pts;
                    int64_t dts = decpkt.dts;
                    int duration = decpkt.duration;
                    int convergence_duration = decpkt.convergence_duration;
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
                    encpkt.pts = pts;
                    encpkt.dts = dts;
                    encpkt.duration = duration;
                    encpkt.convergence_duration = convergence_duration;
                    encframe->pts = encpkt.pts == AV_NOPTS_VALUE ? encframe->pts + 1 : encpkt.pts;
                    ret = avcodec_encode_video2(ovcctx, &encpkt, encframe, &got_frame);
                    if( 0 > ret ) {
                        av_free_packet(&encpkt);
                        goto end;
                    }
                    if(got_frame) {
                        encpkt.stream_index = ovstream->index;
                        ret = av_interleaved_write_frame(ofctx, &encpkt);
                        if( 0 > ret ) {
                            LOGE("Couldn't write video frame");
                            goto end;
                        }
                    }

                    av_free_packet(&encpkt);
                }
            }
            else if(iastream->index == stream_index) { // audio
                LOGI("Audio writing");
                AVPacket apkt = decpkt;
                apkt.stream_index = oastream->index;
                ret = av_write_frame(ofctx, &apkt);
                if( 0 > ret ){
                    LOGE("Couldn't write audio frame");
                    goto end;
                }
                got_frame = -1;
                break;
                av_free_packet(&apkt);
            }
            else { // subtitle, etc...
                got_frame = -1;
                break;
            }
        } while (0 == got_frame);

        av_free_packet(&tmp_pkt);
    }

    ret = av_write_trailer(ofctx);
    if( 0 > ret ){
        LOGE("Couldn't write trailer");
    }
//    ret = av_image_alloc(oframe->data, oframe->linesize, dstw, dsth, dst_pix_fmt, 1);
//    checkend(ret, "Couldn't allocate raw video buffer");
//
//    ret = avformat_write_header(ofctx, NULL);
//    if( 0 > ret ) {
//        LOGE("Error occurred when opening output file: %s", av_err2str(ret));
//        goto end;
//    }
//
//    int total_frame = ivstream->nb_frames;
//    LOGI("Total input video frame count %" PRId64 "", ivstream->nb_frames);
//    int got_frame;
//    ret = av_read_frame(ifctx, &packet);
//    while( 0 <= av_read_frame(ifctx, &packet)) {
//        if(is_finish) {
//            break;
//        }
//        AVPacket tmp_pkt = packet;
//        do {
//            if(is_finish) {
//                break;
//            }
//            int decoded = packet.size;
//            if( video_stream_index == packet.stream_index) {
//                prepare_time();
//                start_time();
//                ret = avcodec_decode_video2(ivcctx, frame, &got_frame, &packet);
//                end_time();
//                print_time("Decoding");
//                checkendr(ret, break, "Couldn't decode video");
//                if(got_frame) {
////                    start_time();
//                    int oh = sws_scale(swsctx, frame->data, frame->linesize, 0, ivcctx->height, oframe->data, oframe->linesize);
////                    end_time();
////                    print_time("Resizing");
//                    int got_packet = 0;
//                    AVPacket pkt = {0};
//                    av_init_packet(&pkt);
//                    pkt.pts = packet.pts;
//                    pkt.dts = packet.dts;
//                    int64_t pts = packet.pts;
//                    int64_t dts = packet.dts;
//                    int duration = packet.duration;
//                    int convergence_duration = packet.convergence_duration;
//                    if(pts != AV_NOPTS_VALUE) {
//                        pts = av_rescale_q(pts, ivcctx->time_base, ovcctx->time_base);
//                    }
//                    if(dts != AV_NOPTS_VALUE) {
//                        dts = av_rescale_q(dts, ivcctx->time_base, ovcctx->time_base);
//                    }
//                    if(duration > 0) {
//                        duration = av_rescale_q(duration, ivcctx->time_base, ovcctx->time_base);
//                    }
//                    if(convergence_duration > 0) {
//                        convergence_duration = av_rescale_q(convergence_duration, ivcctx->time_base, ovcctx->time_base);
//                    }
//                    pkt.pts = pts;
//                    pkt.dts = dts;
//                    pkt.duration = duration;
//                    pkt.convergence_duration = convergence_duration;
//                    oframe->pts = pts;
////                    start_time();
//                    ret = avcodec_encode_video2(ovcctx, &pkt, oframe, &got_packet);
////                    end_time();
////                    print_time("Encoding");
//                    if( 0 > ret ) {
//                        LOGE("Couldn't encoding video frame: %s", av_err2str(ret));
//                        av_free_packet(&pkt);
//                        exit(1);
//                    }
//                    if(got_packet) {
//                        av_copy_packet_side_data(&pkt, &packet);
//                        ret = write_frame(ofctx, &ovcctx->time_base, ovstream, &pkt);
//                    }
//                    else {
//                        ret = 0;
//                    }
//                    log_frame(frame->coded_picture_number, total_frame);
//                    av_free_packet(&pkt);
//                }
//
//                av_frame_unref(frame);
//
//                if( 0 > decoded ) {
//                    break;
//                }
//                packet.data += decoded;
//                packet.size -= decoded;
//            }
//            else if(audio_stream_index == packet.stream_index) {
//                AVPacket apkt = packet;
//                apkt.stream_index = oastream_index;
//                ret = av_write_frame(ofctx, &apkt);
//                if( 0 > ret ) {
//                    LOGE("Couldn't write audio frame : %s", av_err2str(ret));
//                }
//                if( 0 > decoded ) {
//                    break;
//                }
//                packet.data += decoded;
//                packet.size -= decoded;
//            }
//        } while (0 < packet.size);
//        av_free_packet(&tmp_pkt);
//    }
//
//    packet.data = NULL;
//    packet.size = 0;
//    av_free_packet(&packet);
//
//    ret = av_write_trailer(ofctx);
//    if( 0 > ret ) {
//        LOGE("Couldn't write trailer");
//    }

end:

    LOGI("Finished");
    finish();

    av_frame_free(&decframe);
    av_frame_free(&encframe);
//    av_free_packet(&decpkt);

    avcodec_close(ivcctx);
    ivcctx = NULL;
    avcodec_close(iacctx);
    iacctx = NULL;
    avformat_close_input(&ifctx);
    ifctx = NULL;

    avio_close(ofctx->pb);
    avcodec_close(ovcctx);
    ovcctx = NULL;
    avcodec_close(oacctx);
    oacctx = NULL;
    avformat_free_context(ofctx);
    ofctx = NULL;
//    av_free(video_dst_data[0]);
    sws_freeContext(swsctx);
    swsctx = NULL;
    char *error_str = "";
    if( 0 > ret ) {
        error_str = av_err2str(ret);
        LOGE("Error (%s)", av_err2str(ret));
    }
//    finish();
}