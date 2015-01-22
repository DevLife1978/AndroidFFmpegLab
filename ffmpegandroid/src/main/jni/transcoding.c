/*
 * Copyright (c) 2010 Nicolas George
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2014 Andrey Utkin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * API example for demuxing, decoding, filtering, encoding and muxing
 * @example transcoding.c
 */

#include "transcoding.h"
#include <pthread.h>

AVFormatContext *in_fmt_ctx;
AVStream *in_a_stream;
AVStream *in_v_stream;

AVFormatContext *out_fmt_ctx;
AVStream *out_a_stream;
AVStream *out_v_stream;
AVCodecContext *v_enc_ctx;
AVCodecContext *a_enc_ctx;

typedef struct FilteringContext {
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
} FilteringContext;
static FilteringContext *filter_ctx;

static int open_input_file(const char *filename)
{
    int ret;
    unsigned int i;

    in_fmt_ctx = NULL;
    if ((ret = avformat_open_input(&in_fmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(in_fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    for (i = 0; i < in_fmt_ctx->nb_streams; i++) {
        AVStream *stream;
        AVCodecContext *codec_ctx;
        stream = in_fmt_ctx->streams[i];
        codec_ctx = stream->codec;
        /* Reencode video & audio and remux subtitles etc. */
        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
                || codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* Open decoder */
            ret = avcodec_open2(codec_ctx,
                    avcodec_find_decoder(codec_ctx->codec_id), NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return ret;
            }
        }
    }

    av_dump_format(in_fmt_ctx, 0, filename, 0);
    return 0;
}

static int open_output_file(const char *filename)
{
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    int ret;
    unsigned int i;

    out_fmt_ctx = NULL;
    avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, filename);
    if (!out_fmt_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return AVERROR_UNKNOWN;
    }
    for (i = 0; i < in_fmt_ctx->nb_streams; i++) {
        out_stream = avformat_new_stream(out_fmt_ctx, NULL);
        if (!out_stream) {
            av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }
        in_stream = in_fmt_ctx->streams[i];
        dec_ctx = in_stream->codec;
        enc_ctx = out_stream->codec;
        LOGI("Stream #%u", i);
        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
                || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO && enc_ctx) {
            /* in this example, we choose transcoding to same codec */
            const AVCodec *encoder = avcodec_find_encoder(dec_ctx->codec_id);
            if (!encoder) {
                av_log(NULL, AV_LOG_FATAL, "Neccessary encoder not found #%u\n", i);
                return AVERROR_INVALIDDATA;
            }
            /* In this example, we transcode to same properties (picture size,
             * sample rate etc.). These properties can be changed for output
             * streams easily using filters */
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                avcodec_copy_context(enc_ctx, dec_ctx);
//                enc_ctx->height = dec_ctx->height;
//                enc_ctx->width = dec_ctx->width;
//                enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
//                /* take first format from list of supported formats */
//                enc_ctx->pix_fmt = encoder->pix_fmts[0];
//                /* video time_base can be set to whatever is handy and supported by encoder */
//                enc_ctx->time_base = (AVRational){1, 25};
                LOGI("bitrate -> %u", enc_ctx->bit_rate);
                LOGI("samplerate -> %u", enc_ctx->sample_rate);
                LOGI("timebase den -> %d, num -> %d", enc_ctx->time_base.den, enc_ctx->time_base.num);
            } else {
                enc_ctx->sample_rate = dec_ctx->sample_rate;
                enc_ctx->channel_layout = dec_ctx->channel_layout;
                enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
                /* take first format from list of supported formats */
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
                enc_ctx->time_base = dec_ctx->time_base;
            }
            LOGI("Codec id #%u, %d - %d", i, enc_ctx->codec_type, encoder->type);
            /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(enc_ctx, encoder, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u %s\n", i, enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
                return ret;
            }
        } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        } else {
            /* if this stream must be remuxed */
            ret = avcodec_copy_context(out_fmt_ctx->streams[i]->codec,
                    in_fmt_ctx->streams[i]->codec);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Copying stream context failed\n");
                return ret;
            }
        }

        if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;

    }
    av_dump_format(out_fmt_ctx, 0, filename, 1);

    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out_fmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(out_fmt_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }

    return 0;
}

static int init_filter(FilteringContext* fctx, AVCodecContext *dec_ctx,
        AVCodecContext *enc_ctx, const char *filter_spec)
{
    char args[512];
    int ret = 0;
    AVFilter *buffersrc = NULL;
    AVFilter *buffersink = NULL;
    AVFilterContext *buffersrc_ctx = NULL;
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVFilterGraph *filter_graph = avfilter_graph_alloc();

    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                dec_ctx->time_base.num, dec_ctx->time_base.den,
                dec_ctx->sample_aspect_ratio.num,
                dec_ctx->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
                (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (!dec_ctx->channel_layout)
            dec_ctx->channel_layout =
                av_get_default_channel_layout(dec_ctx->channels);
        snprintf(args, sizeof(args),
                "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
                dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
                av_get_sample_fmt_name(dec_ctx->sample_fmt),
                dec_ctx->channel_layout);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
                (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "channel_layouts",
                (uint8_t*)&enc_ctx->channel_layout,
                sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
                (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    } else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec,
                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

    /* Fill FilteringContext */
    fctx->buffersrc_ctx = buffersrc_ctx;
    fctx->buffersink_ctx = buffersink_ctx;
    fctx->filter_graph = filter_graph;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static int init_filters(void)
{
    const char *filter_spec;
    unsigned int i;
    int ret;
    filter_ctx = av_malloc_array(in_fmt_ctx->nb_streams, sizeof(*filter_ctx));
    if (!filter_ctx)
        return AVERROR(ENOMEM);

    for (i = 0; i < in_fmt_ctx->nb_streams; i++) {
        filter_ctx[i].buffersrc_ctx  = NULL;
        filter_ctx[i].buffersink_ctx = NULL;
        filter_ctx[i].filter_graph   = NULL;
        if (!(in_fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO
                || in_fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;


        if (in_fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            filter_spec = "null"; /* passthrough (dummy) filter for video */
        else
            filter_spec = "anull"; /* passthrough (dummy) filter for audio */
        ret = init_filter(&filter_ctx[i], in_fmt_ctx->streams[i]->codec,
                out_fmt_ctx->streams[i]->codec, filter_spec);
        if (ret)
            return ret;
    }
    return 0;
}

static int encode_write_frame(AVFrame *filt_frame, unsigned int stream_index, int *got_frame) {
    int ret;
    int got_frame_local;
    AVPacket enc_pkt;
    int (*enc_func)(AVCodecContext *, AVPacket *, const AVFrame *, int *) =
        (in_fmt_ctx->streams[stream_index]->codec->codec_type ==
         AVMEDIA_TYPE_VIDEO) ? avcodec_encode_video2 : avcodec_encode_audio2;

    if (!got_frame)
        got_frame = &got_frame_local;

    av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
    /* encode filtered frame */
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    av_init_packet(&enc_pkt);
    ret = enc_func(out_fmt_ctx->streams[stream_index]->codec, &enc_pkt,
            filt_frame, got_frame);
    av_frame_free(&filt_frame);
    if (ret < 0)
        return ret;
    if (!(*got_frame))
        return 0;

    /* prepare packet for muxing */
    enc_pkt.stream_index = stream_index;
    av_packet_rescale_ts(&enc_pkt,
                         out_fmt_ctx->streams[stream_index]->codec->time_base,
                         out_fmt_ctx->streams[stream_index]->time_base);

    av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
    /* mux encoded frame */
    ret = av_interleaved_write_frame(out_fmt_ctx, &enc_pkt);
    return ret;
}

static int filter_encode_write_frame(AVFrame *frame, unsigned int stream_index)
{
    int ret;
    AVFrame *filt_frame;

    av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(filter_ctx[stream_index].buffersrc_ctx,
            frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        filt_frame = av_frame_alloc();
        if (!filt_frame) {
            ret = AVERROR(ENOMEM);
            break;
        }
        av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(filter_ctx[stream_index].buffersink_ctx,
                filt_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            av_frame_free(&filt_frame);
            break;
        }

        filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(filt_frame, stream_index, NULL);
        if (ret < 0)
            break;
    }

    return ret;
}

static int flush_encoder(unsigned int stream_index)
{
    int ret;
    int got_frame;

    if (!(out_fmt_ctx->streams[stream_index]->codec->codec->capabilities &
                CODEC_CAP_DELAY))
        return 0;

    while (1) {
        av_log(NULL, AV_LOG_INFO, "Flushing stream #%u encoder\n", stream_index);
        ret = encode_write_frame(NULL, stream_index, &got_frame);
        if (ret < 0)
            break;
        if (!got_frame)
            return 0;
    }
    return ret;
}

int downstream(const char *input, const char *output)
{
    av_register_all();
    avcodec_register_all();
    avfilter_register_all();

    int i = 0;
    int ret = 0;
    in_fmt_ctx = NULL;
    ret = avformat_open_input(&in_fmt_ctx, input, NULL, NULL);
    if(0 > ret) {
        LOGE("Couldn't open input format context -> %s", input);
        goto end;
    }
    ret = avformat_find_stream_info(in_fmt_ctx, NULL);
    if(0 > ret) {
        LOGE("Couldn't find input stream");
        goto end;
    }

    for(i = 0 ; i < in_fmt_ctx->nb_streams ; i++) {
        AVStream *stream = in_fmt_ctx->streams[i];
        switch(stream->codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                in_v_stream = stream;
                ret = avcodec_open2(in_v_stream->codec, avcodec_find_decoder(in_v_stream->codec->codec_id), NULL);
                if(0 > ret) {
                    LOGE("Couldn't open video decoder");
                    goto end;
                }
            break;
            case AVMEDIA_TYPE_AUDIO:
                in_a_stream = stream;
                ret = avcodec_open2(in_a_stream->codec, avcodec_find_decoder(in_a_stream->codec->codec_id), NULL);
                if(0 > ret) {
                    LOGE("Couldn't open audio decoder");
                    goto end;
                }
            break;
        }
    }

    out_fmt_ctx = NULL;
    ret = avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, output);
    if( 0 > ret ) {
        LOGE("Couldn't alloc output format context");
        goto end;
    }

    // for encoder

    AVCodec *v_encoder = NULL;
    AVCodec *a_encoder = NULL;
    v_encoder = avcodec_find_encoder(in_v_stream->codec->codec_id);
    if(!v_encoder) {
        LOGE("Couldn't find video encoder");
        return -1;
    }
    a_encoder = avcodec_find_encoder(in_a_stream->codec->codec_id);
    if(!a_encoder) {
        LOGE("Couldn't find audio encoder");
        return -1;
    }

    out_v_stream = NULL;
    out_v_stream = avformat_new_stream(out_fmt_ctx, v_encoder);
    if(!out_v_stream){
        LOGE("Couldn't new video stream");
        return -1;
    }

    v_enc_ctx = out_v_stream->codec;
    v_enc_ctx->pix_fmt = in_v_stream->codec->pix_fmt;
    v_enc_ctx->width = in_v_stream->codec->width;
    v_enc_ctx->height = in_v_stream->codec->height;

    ret = avcodec_open2(v_enc_ctx, v_encoder, NULL);
    if(0 > ret) {
        LOGE("Couldn't open video encoder");
        goto end;
    }

    out_a_stream = NULL;
    out_a_stream = avformat_new_stream(out_fmt_ctx, a_encoder);
    if(!out_a_stream) {
        LOGE("Couldn't new audio stream");
        return -1;
    }
    ret = avcodec_copy_context(out_a_stream->codec, in_a_stream->codec);
    out_a_stream->codec->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    if(0 > ret) {
        LOGE("Couldn't copy audio context");
        goto end;
    }
    ret = avcodec_open2(out_a_stream->codec, a_encoder, NULL);
    if(0 > ret) {
        LOGE("Couldn't open audio encoder");
        goto end;
    }

    ret = init_filters();
    if( 0 > ret ) {
        LOGE("Couldn't intialize filters");
        goto end;
    }

    AVPacket packet;
    av_init_packet(&packet);

    AVFrame *frame = NULL;
    int got_frame = 0;
    int stream_index = 0;
    while(1) {
        ret = av_read_frame(in_fmt_ctx, &packet);
        if(0 > ret) {
            LOGE("Couldn't read input frame");
            goto end;
        }
        stream_index = packet.stream_index;
        int type = in_fmt_ctx->streams[stream_index]->codec->codec_type;
        switch(type) {
            case AVMEDIA_TYPE_VIDEO:
            {
                LOGI("Demuxer gave frame of video");
                if(filter_ctx[stream_index].filter_graph) {
                    frame = av_frame_alloc();
                    if(!frame) {
                        LOGE("Couldn't alloc video frame");
                        ret = AVERROR(ENOMEM);
                        goto end;
                    }
                    av_packet_rescale_ts(&packet, in_v_stream->time_base, in_v_stream->codec->time_base);
                    ret = avcodec_decode_video2(in_v_stream->codec, frame, &got_frame, &packet);
                    if( 0 > ret ) {
                        av_frame_free(&frame);
                        LOGE("Couldn't decode video");
                        goto end;
                    }
                    if(got_frame) {
                        frame->pts = av_frame_get_best_effort_timestamp(frame);
                        ret = filter_encode_write_frame(frame, stream_index);
                        av_frame_free(&frame);
                        if( 0 > ret ) {
                            LOGE("Couldn't write video with filter");
                            goto end;
                        }
                    }
                    else {
                        av_frame_free(&frame);
                    }
                }
                else {
                    av_packet_rescale_ts(&packet, in_v_stream->time_base, out_v_stream->time_base);
                    ret = av_interleaved_write_frame(out_fmt_ctx, &packet);
                    if( 0 > ret ) {
                        LOGE("Couldn't write video");
                        goto end;
                    }
                }
                av_free_packet(&packet);
            }
            break;
            case AVMEDIA_TYPE_AUDIO:
            {
                LOGI("Demuxer gave frame of audio");
                if(filter_ctx[stream_index].filter_graph) {
                    LOGI("Audio encode with filter");
                    frame = av_frame_alloc();
                    if(!frame) {
                        LOGE("Couldn't alloc audio frame");
                        ret = AVERROR(ENOMEM);
                        goto end;
                    }
                    av_packet_rescale_ts(&packet, in_a_stream->time_base, in_a_stream->codec->time_base);

                    ret = avcodec_decode_audio4(in_a_stream->codec, frame, &got_frame, &packet);

                    if( 0 > ret ) {
                        av_frame_free(&frame);
                        LOGE("Couldn't decode audio");
                        goto end;
                    }
                    if(got_frame) {
                        frame->pts = av_frame_get_best_effort_timestamp(frame);
                        ret = filter_encode_write_frame(frame, stream_index);
                        av_frame_free(&frame);
                        if( 0 > ret ) {
                            LOGE("Couldn't write audio with filter");
                            goto end;
                        }
                    }
                    else {
                        av_frame_free(&frame);
                    }
                }
                else {
                    LOGI("Audio encode");
                    av_packet_rescale_ts(&packet, in_a_stream->time_base, out_a_stream->time_base);
                    ret = av_interleaved_write_frame(out_fmt_ctx, &packet);
                    if( 0 > ret ) {
                        LOGE("Couldn't audio video");
                        goto end;
                    }
                }
                av_free_packet(&packet);
            }
            break;
        }
    }

    for (i = 0; i < in_fmt_ctx->nb_streams; i++) {
        /* flush filter */
        if (!filter_ctx[i].filter_graph)
            continue;
        ret = filter_encode_write_frame(NULL, i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
            goto end;
        }

        /* flush encoder */
        ret = flush_encoder(i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
            goto end;
        }
    }

    av_write_trailer(out_fmt_ctx);
end:
    if(ret < 0 ){
        LOGE("finish with error -> %s", av_err2str(ret));
    }
    else {
        LOGI("<--- finished --->");
    }
    av_free_packet(&packet);
    av_frame_free(&frame);
    for (i = 0; i < in_fmt_ctx->nb_streams; i++) {
        if (filter_ctx && filter_ctx[i].filter_graph)
            avfilter_graph_free(&filter_ctx[i].filter_graph);
    }
    av_free(filter_ctx);
    avcodec_close(in_a_stream->codec);
    avcodec_close(in_v_stream->codec);
    avformat_close_input(&in_fmt_ctx);
    if (out_fmt_ctx && !(out_fmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_close(out_fmt_ctx->pb);
    avformat_free_context(out_fmt_ctx);
    av_free_packet(&packet);
    return ret;
}
//    int ret;
//    AVPacket packet = { .data = NULL, .size = 0 };
//    AVFrame *frame = NULL;
//    enum AVMediaType type;
//    unsigned int stream_index;
//    unsigned int i;
//    int got_frame;
//    int (*dec_func)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
//
//    av_register_all();
//    avfilter_register_all();
//
//    if ((ret = open_input_file(input)) < 0)
//        goto end;
//    if ((ret = open_output_file(output)) < 0)
//        goto end;
//    if ((ret = init_filters()) < 0)
//        goto end;
//
//    /* read all packets */
//    while (1) {
//        if ((ret = av_read_frame(in_fmt_ctx, &packet)) < 0)
//            break;
//        stream_index = packet.stream_index;
//        type = in_fmt_ctx->streams[packet.stream_index]->codec->codec_type;
//        av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame of stream_index %u\n",
//                stream_index);
//
//        if (filter_ctx[stream_index].filter_graph) {
//            av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");
//            frame = av_frame_alloc();
//            if (!frame) {
//                ret = AVERROR(ENOMEM);
//                break;
//            }
//            av_packet_rescale_ts(&packet,
//                                 in_fmt_ctx->streams[stream_index]->time_base,
//                                 in_fmt_ctx->streams[stream_index]->codec->time_base);
//            dec_func = (type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 :
//                avcodec_decode_audio4;
//            ret = dec_func(in_fmt_ctx->streams[stream_index]->codec, frame,
//                    &got_frame, &packet);
//            if (ret < 0) {
//                av_frame_free(&frame);
//                av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
//                break;
//            }
//
//            if (got_frame) {
//                frame->pts = av_frame_get_best_effort_timestamp(frame);
//                ret = filter_encode_write_frame(frame, stream_index);
//                av_frame_free(&frame);
//                if (ret < 0)
//                    goto end;
//            } else {
//                av_frame_free(&frame);
//            }
//        } else {
//            /* remux this frame without reencoding */
//            av_packet_rescale_ts(&packet,
//                                 in_fmt_ctx->streams[stream_index]->time_base,
//                                 out_fmt_ctx->streams[stream_index]->time_base);
//
//            ret = av_interleaved_write_frame(out_fmt_ctx, &packet);
//            if (ret < 0)
//                goto end;
//        }
//        av_free_packet(&packet);
//    }
//
//    /* flush filters and encoders */
//    for (i = 0; i < in_fmt_ctx->nb_streams; i++) {
//        /* flush filter */
//        if (!filter_ctx[i].filter_graph)
//            continue;
//        ret = filter_encode_write_frame(NULL, i);
//        if (ret < 0) {
//            av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
//            goto end;
//        }
//
//        /* flush encoder */
//        ret = flush_encoder(i);
//        if (ret < 0) {
//            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
//            goto end;
//        }
//    }
//
//    av_write_trailer(out_fmt_ctx);
//end:
//    av_free_packet(&packet);
//    av_frame_free(&frame);
//    for (i = 0; i < in_fmt_ctx->nb_streams; i++) {
//        avcodec_close(in_fmt_ctx->streams[i]->codec);
//        if (out_fmt_ctx && out_fmt_ctx->nb_streams > i && out_fmt_ctx->streams[i] && out_fmt_ctx->streams[i]->codec)
//            avcodec_close(out_fmt_ctx->streams[i]->codec);
//        if (filter_ctx && filter_ctx[i].filter_graph)
//            avfilter_graph_free(&filter_ctx[i].filter_graph);
//    }
//    av_free(filter_ctx);
//    avformat_close_input(&in_fmt_ctx);
//    if (out_fmt_ctx && !(out_fmt_ctx->oformat->flags & AVFMT_NOFILE))
//        avio_close(out_fmt_ctx->pb);
//    avformat_free_context(out_fmt_ctx);
//
//    if (ret < 0)
//        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
//
//    return ret ? 1 : 0;
//}
