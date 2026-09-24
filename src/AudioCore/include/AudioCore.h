#pragma once
#include <stdint.h>
#include <wchar.h>
#ifdef AUDIOCORE_EXPORTS
#define AB_API __declspec(dllexport)
#else
#define AB_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define AB_VERSION 1u
typedef void* AB_Handle;
enum AB_Result { AB_OK=0, AB_INVALID_ARGUMENT=-1, AB_BUFFER_TOO_SMALL=-2, AB_BUSY=-3, AB_FAILURE=-4 };
enum AB_State { AB_STOPPED=0, AB_CONNECTING=1, AB_READY=2, AB_STREAMING=3, AB_STOPPING=4, AB_ERROR=5 };
#pragma pack(push, 8)
typedef struct AB_Device { wchar_t id[128]; wchar_t name[128]; wchar_t model[64]; wchar_t os[64]; uint32_t paired; uint32_t accessible; } AB_Device;
typedef struct AB_OutputDevice { wchar_t id[512]; wchar_t name[256]; uint32_t is_default; uint32_t reserved; } AB_OutputDevice;
typedef struct AB_Config {
    uint32_t struct_size, version;
    wchar_t device_id[128], output_id[512];
    uint32_t startup_frames, high_water_frames; // 0 uses 3072 / 4096. Capacity is 8192.
    float volume; uint32_t reserved;
} AB_Config;
typedef struct AB_Status {
    uint32_t struct_size, version, state; int32_t error_code;
    uint32_t sample_rate, channels, bits, output_active;
    uint32_t engine_period_frames, endpoint_frames, queued_frames, capacity_frames;
    uint64_t audio_packets, video_packets, video_bytes, rendered_frames, dropped_frames, underruns, missing_frames;
    float input_left_db, input_right_db, output_left_db, output_right_db;
    float host_latency_ms, stream_latency_ms;
    uint32_t autoconvert, source_idle;
    wchar_t message[512];
} AB_Status;
#pragma pack(pop)
// Caller owns buffers. *count is input capacity / output required count. NULL queries size.
AB_API int32_t __cdecl ab_list_devices(AB_Device* items, uint32_t* count);
AB_API int32_t __cdecl ab_list_outputs(AB_OutputDevice* items, uint32_t* count);
AB_API int32_t __cdecl ab_create(AB_Handle* handle);
AB_API int32_t __cdecl ab_start(AB_Handle handle, const AB_Config* config);
// start returns after launching the worker; poll status for Ready/Streaming or Error.
// stop is synchronous, including USB restoration. Call off the WPF dispatcher.
AB_API int32_t __cdecl ab_stop(AB_Handle handle);
AB_API void __cdecl ab_destroy(AB_Handle handle);
// Destroy (or stop then destroy) every handle before unloading AudioCore.dll.
AB_API int32_t __cdecl ab_get_status(AB_Handle handle, AB_Status* status);
AB_API int32_t __cdecl ab_set_volume(AB_Handle handle, float volume);
AB_API int32_t __cdecl ab_last_error(wchar_t* buffer, uint32_t capacity);
// Error text is thread-local; retrieve it immediately on the calling thread.
AB_API uint32_t __cdecl ab_abi_size(uint32_t structure); // 1 Device, 2 Output, 3 Config, 4 Status
#ifdef __cplusplus
}
#endif
