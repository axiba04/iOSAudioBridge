#include "Decoder.h"
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct AB_EldDecoder { AVCodecContext *codec; AVPacket *packet; AVFrame *frame; };
AB_EldDecoder *ab_eld_open(void) {
    static const uint8_t asc[] = {0xf8,0xe6,0x50,0x00};
    AB_EldDecoder *d = calloc(1,sizeof *d);
    if(!d) return NULL;
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if(!codec) goto fail;
    d->codec = avcodec_alloc_context3(codec);
    d->packet = av_packet_alloc(); d->frame = av_frame_alloc();
    if(!d->codec || !d->packet || !d->frame) goto fail;
    d->codec->extradata = av_mallocz(sizeof asc + AV_INPUT_BUFFER_PADDING_SIZE);
    if(!d->codec->extradata) goto fail;
    memcpy(d->codec->extradata,asc,sizeof asc); d->codec->extradata_size = sizeof asc;
    d->codec->sample_rate = 48000; av_channel_layout_default(&d->codec->ch_layout,2);
    if(avcodec_open2(d->codec,codec,NULL) < 0) goto fail;
    return d;
fail: ab_eld_close(d); return NULL;
}
int ab_eld_decode(AB_EldDecoder *d,const uint8_t *data,size_t size,AB_PcmSink sink,void *ctx) {
    if(!size || size > 65535 || av_new_packet(d->packet,(int)size) < 0) return -1;
    memcpy(d->packet->data,data,size);
    int rc = avcodec_send_packet(d->codec,d->packet); av_packet_unref(d->packet);
    if(rc < 0) return -1;
    while((rc = avcodec_receive_frame(d->codec,d->frame)) == 0) {
        AVFrame *f = d->frame;
        if(f->format != AV_SAMPLE_FMT_FLTP || f->sample_rate != 48000 ||
           f->ch_layout.nb_channels != 2 || f->nb_samples != 480) return -1;
        int16_t pcm[480*2];
        for(int c=0;c<2;++c) for(int i=0;i<480;++i) {
            float sample = ((const float *)f->extended_data[c])[i];
            if(!isfinite(sample)) sample=0;
            pcm[i*2+c] = (int16_t)lrintf(fminf(32767.f,fmaxf(-32768.f,sample*32768.f)));
        }
        if(sink(ctx,pcm,sizeof pcm) != 0) return -1;
    }
    return rc == AVERROR(EAGAIN) ? 0 : -1;
}
void ab_eld_close(AB_EldDecoder *d) {
    if(!d) return;
    avcodec_free_context(&d->codec); av_packet_free(&d->packet); av_frame_free(&d->frame); free(d);
}
