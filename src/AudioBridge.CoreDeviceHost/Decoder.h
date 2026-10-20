#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct AB_EldDecoder AB_EldDecoder;
typedef int (*AB_PcmSink)(void *, const int16_t *, size_t);
AB_EldDecoder *ab_eld_open(void);
int ab_eld_decode(AB_EldDecoder *, const uint8_t *, size_t, AB_PcmSink, void *);
void ab_eld_close(AB_EldDecoder *);
