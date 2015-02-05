#include "transcoding.h"


#define checkend(value, ...) if( 0 > value ) { LOGE(__VA_ARGS__); goto end; }
#define checkendr(value, result, ...) if( 0 > value ) { LOGE(__VA_ARGS__); result; }

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
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

int transcoding(const char * input, const char * output) {
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

    ret = avcodec_open2(ivcctx, ivdecoder, NULL);
    checkend(ret, "Couldn't open %s video decoder", ivdecoder->name);

    AVStream *iastream = ifctx->streams[audio_stream_index];
    AVCodecContext *iacctx = iastream->codec;
    LOGI("Find audio stream for %s", iadecoder->name);

    ret = avcodec_open2(iacctx, iadecoder, NULL);
    checkend(ret, "Couldn't open %s audio decoder", iadecoder->name);

    int dstw = 1280;
    int dsth = 720;
//    dstw += dstw % 16;
//    dsth += dsth % 16;
    int dst_pix_fmt = ivcctx->pix_fmt;

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
    AVCodec *venc = avcodec_find_encoder(ofmt->video_codec);
    AVStream *ovstream = avformat_new_stream(ofctx, venc);
    ovstream->id = ofctx->nb_streams-1;

    AVFrame *oframe = av_frame_alloc();
    AVCodecContext *ovcctx = ovstream->codec;
    if(ofmt) {
        LOGI("output video codec -> %s", avcodec_get_name(ofmt->video_codec));
        LOGI("output audio codec -> %s", avcodec_get_name(ofmt->audio_codec));

        ret = avcodec_get_context_defaults3(ovcctx, venc);
        if( 0 > ret) {
            goto end;
        }

        ovcctx->bit_rate = 400000;
        ovcctx->width = dstw;
        ovcctx->height = dstw;
        ovstream->time_base = (AVRational){1, 25};
        ovcctx->time_base = ovstream->time_base;
        ovcctx->gop_size = ivcctx->gop_size;
        ovcctx->pix_fmt = ivcctx->pix_fmt;

        if(ofctx->oformat->flags & AVFMT_GLOBALHEADER) {
            ovcctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }

        ret = avcodec_open2(ovcctx, venc, NULL);
        if( 0 > ret ) {
            goto end;
        }
        oframe->width = ovcctx->width;
        oframe->height = ovcctx->height;
        oframe->format = ovcctx->pix_fmt;

         /* allocate image where the decoded image will be put */
//        ret = av_image_alloc(video_dst_data, video_dst_linesize,
//                             dstw, dsth,
//                             dst_pix_fmt, 1);
//        if (ret < 0) {
//            fprintf(stderr, "Could not allocate raw video buffer\n");
//            goto end;
//        }
//        video_dst_bufsize = ret;

        ret = av_frame_get_buffer(oframe, 0);
        if( 0 > ret ) {
            LOGE("Couldn't create output frame buffer");
            goto end;
        }
    }

    ret = avio_open(&ofctx->pb, output_path, AVIO_FLAG_WRITE);
    if( 0 > ret ) {
        LOGE("Couldn't open write file");
        goto end;
    }

//    FILE *oldfile = fopen("/sdcard/Movies/video.tmp", "r");
//    if(oldfile) {
//        fclose(oldfile);
//        remove("/sdcard/Movies/video.tmp");
//    }
//
//    oldfile = NULL;
//    oldfile = fopen("/sdcard/Movies/audio.tmp", "r");
//    if(oldfile) {
//        fclose(oldfile);
//        remove("/sdcard/Movies/audio.tmp");
//    }
//
//    FILE *videof = fopen("/sdcard/Movies/video.tmp", "wb");
//    if(!videof) {
//        LOGE("Couldn't open output video file");
//        goto end;
//    }
//
//    FILE *audiof = fopen("/sdcard/Movies/audio.tmp", "wb");
//    if(!audiof) {
//        LOGE("Couldn't open output audio file");
//        goto end;
//    }

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
        return 1;
    }
    int pts = 0;
    int got_frame;
    while( 0 <= av_read_frame(ifctx, &packet)) {
        AVPacket tmp_pkt = packet;
        do {
            int decoded = packet.size;
            if( video_stream_index == packet.stream_index) {
                ret = avcodec_decode_video2(ivcctx, frame, &got_frame, &packet);
                checkendr(ret, break, "Couldn't decode video");
                if(got_frame) {
                    sws_scale(swsctx, frame->data, frame->linesize, 0, ivcctx->height, oframe->data, oframe->linesize);
                    int got_packet = 0;
                    AVPacket pkt = {0};
                    av_init_packet(&pkt);
                    oframe->pts = frame->coded_picture_number;
                    ret = avcodec_encode_video2(ovcctx, &pkt, oframe, &got_packet);
                    if( 0 > ret ) {
                        LOGE("Couldn't encoding video frame: %s", av_err2str(ret));
                        av_free_packet(&pkt);
                        exit(1);
                    }
                    if(got_packet) {
                        ret = write_frame(ofctx, &ovcctx->time_base, ovstream, &pkt);
                    }
                    else {
                        ret = 0;
                    }
                    av_free_packet(&pkt);

//                    fwrite(video_dst_data[0], 1, video_dst_bufsize, videof);
                }

                av_frame_unref(frame);

                if( 0 > decoded ) {
                    break;
                }
                packet.data += decoded;
                packet.size -= decoded;
            }
            else {

                ret = avcodec_decode_audio4(iacctx, frame, &got_frame, &packet);
                checkendr(ret, break, "Couldn't decode audio");
                decoded = FFMIN(ret, packet.size);

                if(got_frame) {
                    size_t unpadded_lsz = frame->nb_samples * av_get_bytes_per_sample(frame->format);
                    LOGI("Audio decoded -> %d", frame->nb_samples);

//                    fwrite(frame->extended_data[0], 1, unpadded_lsz, audiof);
                }
                av_frame_unref(frame);
                if( 0 > decoded ) {
                    break;
                }
                packet.data += decoded;
                packet.size -= decoded;
                continue;
            }
        } while (0 < packet.size);
        av_free_packet(&tmp_pkt);
    }

    packet.data = NULL;
    packet.size = 0;

    ret = avcodec_decode_video2(ivcctx, frame, &got_frame, &packet);
    if(got_frame) {
        LOGI("Video flushing decoded -> %d", frame->coded_picture_number);
        sws_scale(swsctx, (const uint8_t * const *)frame->data, frame->linesize, 0, ivcctx->height, oframe->data, oframe->linesize);

        int got_packet = 0;
        AVPacket pkt = {0};
        av_init_packet(&pkt);
        oframe->pts = frame->coded_picture_number;
        ret = avcodec_encode_video2(ovcctx, &pkt, oframe, &got_packet);
        if( 0 > ret ) {
            LOGE("Couldn't encoding video frame: %s", av_err2str(ret));
            av_free_packet(&pkt);
            exit(1);
        }
        if(got_packet) {
            ret = write_frame(ofctx, &ovcctx->time_base, ovstream, &pkt);
        }
        else {
            ret = 0;
        }
        av_free_packet(&pkt);

//        fwrite(video_dst_data[0], 1, video_dst_bufsize, videof);
    }

    av_frame_unref(frame);

    ret = av_write_trailer(ofctx);
    if( 0 > ret ) {
        LOGE("Couldn't write trailer");
    }

end:

    LOGI("Play the output video file with the command:\n"
                   "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
                   av_get_pix_fmt_name(dst_pix_fmt), dstw, dsth,
                   "video.tmp");

    LOGI("Finished");

//    if(videof) {
//        fclose(videof);
//    }
//
//    if(audiof) {
//        fclose(audiof);
//    }

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
    if( 0 > ret ) {
        LOGE("Error (%s)", av_err2str(ret));
    }
}