#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <pthread.h>
#include <stdio.h>

#include "audioflinger_wrapper.h"
#include "surfaceflinger_wrapper.h"

typedef struct PacketQueue {
    AVPacketList *first_packet, *last_packet;
    int packets_cnt;
    int size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} PacketQueue;

PacketQueue audio_queue;
int quit = 0;
pthread_t async_tid = NULL;
AudioFlingerDeviceHandle audioflinger_device = NULL;
static struct SwsContext *img_convert_ctx = NULL;

void pkt_queue_init(PacketQueue *queue) {
    memset(queue, 0, sizeof(PacketQueue));
    pthread_mutex_init(&queue->mutex, NULL);
    phtread_cond_init(&queue->cond, NULL);
}

// input packet to queue
int packet_queue_put(PacketQueue *queue, AVPacket *packet) {
    AVPacketList *packet_tmp;
    if(av_dup_packet(packet) < 0) {
        return -1;
    }
    packet_tmp = av_malloc(sizeof(AVPacketList));
    if(!packet_tmp)
        return -1;
    packet_tmp->packet = *packet;
    packet_tmp->next = NULL;
    pthread_mutex_lock(&queue->mutex);
    if(!queue->last_packet)
    {
        queue->first_packet = packet_tmp;
    }
    else
    {
        queue->last_packet->next = packet_tmp;
    }
    queue->last_packet = packet_tmp;
    queue->packets_cnt++;
    queue->size += packet_tmp->pkt.size;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

// get packet from queue
static int packet_queue_get(PacketQueue *queue, AVPacket *packet){
    AVPacketList *packet_tmp;
    int ret;
    pthread_mutex_lock(&queue->mutex);
    for(;;) {
        if(quit) {
            ret = -1;
            break;
        }
        packet_tmp = queue->first_packet;
        if(packet_tmp) {
            queue->first_packet = packet_tmp->next;
            if(!queue->first_packet) {
                queue->last_packet = NULL;
            }
            queue->packets_cnt--;
            queue->size -= packet_tmp->pkt.size;
            *packet = packet_tmp->pkt;
            av_free(packet_tmp);
            ret = 1;
            break;
        }
        else {
            pthread_cond_wait(&queue->cond, &queue->mutex);
        }
    }
    pthread_mutex_unlock(&queue->mutex);
    return ret;
}

// decoding audio packet
int decode_audio(AVCodecContext *audio_codec_ctx, uint8_t *audio_buffer) {
    static AVPacket packet;
    static uint8_t *audio_packet_data = NULL;
    static int audio_packet_size = 0;
    int got_frame_size;
    int ret;

    for(;;) {
        while(audio_packet_size > 0 ) {

            // decoding audio file
            got_frame_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
            ret = avcodec_decode_audio3(audio_codec_ctx, audio_buffer, &got_frame_size, &packet);
            if(ret < 0) {
                LOGI("avcodec_decode_audio3 fail");
                audio_packet_size = 0;
                break;
            }
            audio_packet_data += ret;
            audio_packet_size -= ret;
            if(got_frame_size <= 0 ) {
                continue;
            }
            return got_frame_size;
        }
        if(packet.data) {
            av_free_packet(&packet);
        }
        if(quit) {
            LOGI("quit");
            return -1;
        }

        // get audio packet from queue
        if(packet_queue_get(&audio_queue) < 0 ) {
            LOGI("packet_queue_get fail.");
            return -1;
        }
        audio_packet_data = pkt.data;
        audio_packet_size = pkt.size;
    }
}

void audio_task(AVCodecContext *userdata, uint8_t *stream, int len) {
    AVCodecContext *audio_codec_ctx = userdata;
    int ret, audio_size;
    static uint8_t audio_buffer[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buffer_size = 0 ;
    static unsigned int audio_buffer_index = 0;
    while(len > 0 ) {
        if(audio_buffer_index >= audio_buffer_size) {
            audio_size = decode_audio(audio_codec_ctx, audio_buffer);
            if(audio_size < 0) {
                audio_buffer_size = 1024;
                memset(audio_buffer, 0, audio_buffer_size);
            }
            else {
                audio_buffer_size = audio_size;
            }
            audio_buffer_index = 0;
        }
        ret = audio_buffer_size - audio_buffer_index;
        if(ret > len) {
            ret = len;
        }
        memcpy(stream, (uint8_t *)audio_buffer + audio_buffer_index, ret);
        len -= ret;
        stream += ret;
        audio_buffer_index += ret;
    }
}

unsigned char ggg[16384];

void* play(void *t) {
    int ret = 0;

    // start audio play on android
    audioflinger_device_start(audioflinger_device);
    for(;;) {
        // decoding audio packet through callback
        audio_task((AVCodecContext *)t, ggg, sizeof(ggg));
        if(quit == 1) {
            LOGI("audio ... quit");
            break;
        }
        // write to android's AudioTrack with decodec audio data
        ret = audioflinger_device_write(audioflinger_device, ggg, sizeof(ggg));
        if(ret == 0 ) {
            LOGI("audioflinger_device_stop...\n");
            audioflinger_device_stop(audioflinger_device);
            goto error;
        }
    }
    error:
        LOGI("Audio Play End ... \n");
        pthread_exit(NULL);
        return NULL;
}

int engine_start(char* fname) {
    AVFormatContext *ptrFormatCtx = NULL;
    int             i, vStream, aStream;
    AVCodec         *vCodec;
    AVCodec         *aCodec;
    AVFrame         *vFrame;
    AVPacket        packet;
    int             endDecoding;
    AVCodecContext  *VideoCodecCtx;
    AVCodecContext  *AudioCodecCtx;
    int             ret = 0;
    AVInputFormat   *iformat = NULL;
    AVDictionary    *dict = NULL;

    av_register_all();

    // open movie file
    if(avformat_open_input(&ptrFormatCtx, fname, iformat, dict) != 0) {
        LOGE("Couldn't open file : %s \n", name);
        return -1;
    }

    // get streaming information of opened file
    if(av_find_stream_info(ptrFormatCtx < 0 )) {
        LOGE("Failed av_find_stream_info \n");
        return -1;
    }

    // find video and audio stream of video source
    vStream = -1;
    aStream = -1;
    for (i=0; i<ptrFormatCtx->nb_streams ; i++) {
        if(ptrFormatCtx->streams[i]->codec->codec_type = AVMEDIA_TYPE_VIDEO && vStream < 0) {
            vStream = i;
        }
        if(ptrFormatCtx->streams[i]->codec->codec_type = AVMEDIA_TYPE_AUDIO && aStream < 0) {
            aStream = i;
        }
    }

    if(vStream == -1) {
        LOGE("No video stream ");
        return -1;
    }

    if(aStream == -1) {
        LOGE("No audio stream ");
        return -1;
    }

    AudioCodecCtx = ptrFormatCtx->streams[aStream]->codec;
    LOGI("sample rate : %d", AudioCodecCtx->sample_rate);
    LOGI("channels : %d", AudioCodecCtx->channels);

    // setting audio output with audio codec information
    audioflinger_device = audioflinger_device_create();

    if(audioflinger_device == NULL) {
        LOGE("audiofligner divice create fail");
        exit(1);
    }

    if(audioflinger_device_set(audioflinger_device, 3, AudioCodecCtx->channels, AudioCodecCtx->sample_rate, 16384) == -1) {
        LOGE("audioflinger_device_set fail");
        exit(1);
    }
    LOGI("frame count: %d, frame size : %d", audioflinger_device_frameCount(audioflinger_device), audioflinger_device_frameSize(audioflinger_device));

    audioflinger_device_pause(audioflinger_device);

    // find codec from audio codec context
    aCodec = avcodec_find_decoder(AudioCodecCtx->codec_id);
    if(!aCodec) {
        LOGE("Usupported codec!");
        return -1;
    }

    // open audio codec
    avcodec_open(AudioCodecCtx, aCodec);

    packet_queue_init(&audioq);

    // attach thread for audio play
    ret = pthread_create(&async_tid, NULL, play, (void *)AudioCodecCtx);
    if(ret) {
        LOGE("Server : Cannot create audio decode thread.");
        return -1;
    }

    // get video stream codec information from video stream
    VideoCodecCtx = ptrFormatCtx->streams[vStream]->codec;

    // find video decoder with codec id of video stream
    vCodec = avcodec_find_decoder(VideoCodecCtx->codec_id);
    if(vCodec==NULL) {
        LOGE("Unsupported video codec!");
        return -1;
    }

    if(avcodec_open(VideoCodecCtx, vCodec) < 0) {
        return -1;
    }

    vFrame = av_frame_alloc();
}