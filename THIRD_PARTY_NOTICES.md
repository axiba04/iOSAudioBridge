# Third-party notices

iOSAudioBridge is a GPL-3.0 derivative of the verified audio-only iPhoneMirror
branch. The root `LICENSE` contains the GPL text. Native protocol, CoreMedia,
USB transport/discovery, restore helper and logging code originate from
[RayrenSX/iPhoneMirror](https://github.com/RayrenSX/iPhoneMirror), base commit
`1270c443a29457751cadd20f6d7bbe1ce213b9db` plus the user's local audio-only changes.
The unmodified selected source snapshot, SHA-256 manifest and local patch are
in `third_party/iphone-mirror-baseline/`. The modified implementation is in
`src/AudioCore/` and `src/AudioBridge.UsbRestore/`. Local changes remove video
negotiation/decoding, expose a C ABI, replace the audio queue/rendering, and
integrate the ZL application described below. Changes were made on 2026-09-24.

## libusb-win32 1.4.0.2

The application dynamically links the x64 `libusb0.dll` compatibility runtime.
The runtime, header and import library come from the official 1.4.0.2 binary
archive. The x64 MSVC import library is stored as `libusb0-dynamic.lib` to retain
the existing build layout. Source, file hashes and release URLs are recorded in
`third_party/libusb-win32/pins.json`. No kernel driver is installed by this app;
the separate driver trial scripts require explicit administrator execution.

- Project: https://github.com/mcuee/libusb-win32
- Release: https://github.com/mcuee/libusb-win32/releases/tag/release_1.4.0.2
- Library license: LGPL; the upstream archive includes LGPL version 3 and GPL
  license texts, preserved here together with the original source notices.
- Licenses/authors: `third_party/libusb-win32/COPYING_LGPL.txt`,
  `COPYING_GPL.txt`, `AUTHORS.txt`, `README.txt`, `CHANGELOG.txt`.
- Corresponding upstream source: `third_party/libusb-win32/libusb-win32-src-1.4.0.2.zip`,
  commit `0987f983a89e5e60d3c2db2af46564239c1b0d37`.
  This archive is also included with the local application package in `licenses/`.

## Driver installation kit

`DriverInstallKit/` preserves the original signed Apple Mobile Device Support
MSI and AppleUSB driver package from the user's original driver kit. Apple
software remains subject to Apple's copyright and terms, not this project's
GPL license. The libusb kernel driver and filter installer come from the same
1.4.0.2 upstream release described above; their original GPL/LGPL notices and
corresponding source are included under `DriverInstallKit/licenses/`.
The new PowerShell installation scripts are part of this GPL-3.0 project.

## Protocol fixtures

`tests/fixtures/quicktime_video_hack/` contains unmodified interoperability
fixtures from https://github.com/danielpaulus/quicktime_video_hack.
Copyright (c) 2019 danielpaulus; MIT license, included alongside the fixtures.

The current application does not bundle .NET, WPF, FFmpeg, AirPlay, libusb-1.0
or a VST host. Microsoft/Apple device support
and the existing libusb filter must already be installed on the test machine.

## ZL USB Audio Bridge (2026-09-24 integration)

This native application uses ZLCompressor's processor and editor with
the shared iPhone USB/WASAPI backend. It is a modified community integration,
not an official ZL-Audio release. New application and adapter code in
`src/ZLUsbAudioBridge/` is provided under AGPL-3.0; original component notices
remain intact. The combined application follows the AGPL source distribution
path; the existing backend retains its GPL notices.

- ZLCompressor: https://github.com/ZL-Audio/ZLCompressor, AGPL-3.0.
  Version 0.6.1, commit `6d8c79cff62b42b1d5e4cf60c8eda2ca90b7e938`, updated 2026-10-03.
- JUCE: version 9.0.2, pinned ZL-Audio fork commit `e66b82515320d92a2224651f8ecc1c888a8ec3d8`.
  The actual submodule `JUCE/LICENSE.md` is copied, using its AGPL option.
  The stale top-level ZL copy of JUCE's license is not used for packaging.
  Its dependency inventory `JUCE.spdx.json` is also included in the package.
- Highway: `2607d3b5b0113992fe84d3848859eae13b3b52c1`, Apache-2.0.
- zldsp_fft: `b741fb7ac6ecdb9ea935730aa34fa89602f3e52f`, Apache-2.0,
  header-only, depends on Highway; no further git submodules found.
- Material Design icons and Pamplejuce: original notices from ZL's `licenses/`.
- Inter font subsets are reused unchanged from ZL's assets. SIL OFL 1.1 notice
  copied from https://github.com/rsms/inter/blob/master/LICENSE.txt on 2026-09-24
  to `third_party/zl-bridge-licenses/Inter-OFL.txt`.
- JUCE's bundled component license files are copied with their original paths
  into `licenses/JUCE-components`; this includes optional source components
  and does not imply all plugin formats/backends are active.

The source checkout is `third_party/ZLCompressor/`; exact pins are in
`third_party/ZLCompressor.pins.json` and local changes are exported in
`third_party/ZLCompressor.bridge.patch`. Changes rename the product, fix embedded
version lookup using the dependency pins, constrain latency settings/state loading and provide a hidden
editor test refresh hook. Root CMake provides the custom Standalone entry point.
The complete project source plus these pinned dependency checkouts and build
scripts are the corresponding source material; license files alone are not a
substitute for it when redistributing binaries.

When redistributing this application's binaries, provide the corresponding
source tree and build scripts under their included licenses, not just the EXE.
The selected snapshot's original notices describe additional iPhoneMirror
components and do not imply those components are bundled here.
