# rPlayHub CoreDevice audio engine

Source: https://github.com/rPlayAI/rPlayHub (GPL-3.0; see LICENSE).
Upstream archive commit: `fe6a7a9f1cff431acfda142cd3564fc8e0e849a3`.
Selected C protocol/engine files copied from the user's local rPlayHub working
tree on 2026-10-20. No Python runtime, Apple binary, or video decoder is included.
The local snapshot includes the verified post-DDI RSD refresh fix: after mounting
or confirming DDI, enumerate services again before starting the audio stream.
The user confirmed this path played iPhone audio on Windows.
The reveal-developer-mode helper's completion text points to iOSAudioBridge.

The source files retain the rPlayHub directory layout for relative includes.
`cdhost.c` is compiled with main renamed to `cdhost_main`. The new entry point and
`api_serve` in `src/AudioBridge.CoreDeviceHost/main.c` negotiate only an audio
session, decode AAC-ELD and send PCM16 to the parent via a private pipe. The
original API server/video proxy is not imported or built. No public listening
port is opened by the adapter. The engine uses libimobiledevice, OpenSSL,
libplist, zlib, lwIP 2.2.1, and stock FFmpeg.

`deps/lwip` is downloaded from the upstream STABLE-2_2_1_RELEASE tag by the build
script and is not tracked. DDI files are downloaded or copied into the runtime
folder; Apple's files are not committed to this source snapshot.

Windows runtime dependencies are built/packaged from MSYS2 UCRT64. Keep their
license notices with binary distributions and provide corresponding source/build
materials as required. This snapshot is an integration of rPlayHub, not an
official upstream release.
