#pragma once
#include <stdint.h>

/* Private child stdout stream. Both endpoints are Windows little-endian.
 * Audio payloads are interleaved PCM16, 48000 Hz, stereo; no video messages. */
#define AB_CD_MAGIC 0x44434241u
#define AB_CD_STATUS 1u
#define AB_CD_AUDIO 2u
#define AB_CD_ERROR 3u
#define AB_CD_MAX_PAYLOAD 8192u
typedef struct AB_CD_Header { uint32_t magic, type, size; } AB_CD_Header;
