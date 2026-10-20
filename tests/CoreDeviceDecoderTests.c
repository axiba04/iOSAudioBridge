#include "Decoder.h"
#include <stdio.h>
#include <stdlib.h>
static int frames,nonzero;
static int sink(void *ctx,const int16_t *samples,size_t bytes) {
    (void)ctx;
    if(bytes!=480*2*sizeof(int16_t)) return -1;
    for(size_t i=0;i<480*2;++i) if(samples[i])nonzero=1;
    ++frames;return 0;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    FILE *input=fopen(argv[1],"rb");if(!input)return 2;
    AB_EldDecoder *decoder=ab_eld_open();if(!decoder)return 1;
    unsigned char header[6],data[65535];int packets=0;
    while(fread(header,1,6,input)==6){
        size_t n=((size_t)header[0]<<8)|header[1];
        if(fread(data,1,n,input)!=n || ab_eld_decode(decoder,data,n,sink,NULL)!=0)return 1;
        ++packets;
    }
    fclose(input);
    if(packets<30 || frames!=packets || !nonzero)return 1;
    const unsigned char invalid=0xff;
    if(ab_eld_decode(decoder,&invalid,1,sink,NULL)==0)return 1;
    ab_eld_close(decoder);
    printf("PASS: %d AAC-ELD frames decoded to non-silent 48 kHz stereo PCM16; invalid packet rejected\n",frames);
    return 0;
}
