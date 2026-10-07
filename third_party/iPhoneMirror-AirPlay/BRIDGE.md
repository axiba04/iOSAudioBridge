# iOS Audio Bridge AirPlay dependency snapshot

Imported on 2026-10-07 from RayrenSX/iPhoneMirror commit
`1270c443a29457751cadd20f6d7bbe1ce213b9db` (GPL-3.0-only).
The imported `src/WirelessHost` and `third_party/airplay-server` directories
were clean at that commit in the local source checkout. The snapshot includes
the upstream IPC v7 header, host source, DNS-SD shim, receiver DLLs, source
patches, license notices and receiver rebuild script. File hashes after the
following integration changes are recorded in `pins.json`.

Local changes:

- `WirelessHost/WirelessHost.cpp`: `ZL_AIRPLAY_AUDIO_ONLY` skips video callbacks
  and disables the DLNA renderer independently of the AirPlay receiver mode;
  use a separate stable identity and pairing salt for ZL Audio Bridge.
- `scripts/build_airplay_receiver.ps1`: adjust the receiver path to this layout.
- Build the host and Windows DNS-SD shim through
  `src/AudioBridge.AirPlayHost/CMakeLists.txt`, with static CRT. The original
  upstream CMake file and tests remain here for reference.

The bridge launches `AudioBridge.AirPlayHost.exe` in combined mode, with RAOP
port 5002 and AirPlay port 7002. The upstream mirror mode deliberately rejects
audio-only SETUP/ANNOUNCE with RTSP 403, so it cannot be used for music reception.
The bridge build disables DLNA even in combined mode.
Use the iOS **Now Playing → AirPlay audio** picker. Selecting Screen
Mirroring may still cause the sender/receiver to transmit/decode video; the
bridge drops those frames. This integration does not advertise a new,
unverified audio-only protocol feature mask.

AirPlayServer itself is pinned to v1.1.2, commit
`34ba6cfd49b2432cf30e89913d66decb775763e4`, with iPhoneMirror's ALAC,
negotiation, networking and other patches. See `airplay-server/SOURCE.md` for
the full source and component notices and `airplay-server/SHA256SUMS.txt` for
the original runtime hashes. The original `dnssd.dll` is preserved in the
snapshot, but distribution uses the shim rebuilt from the included source.

The receiver DLL can be rebuilt with the included
`scripts/build_airplay_receiver.ps1` using the Visual Studio tools specified by
that upstream script. Its source download, patch steps and original FFmpeg
libraries come from the pinned AirPlayServer repository. Refresh `pins.json`
after intentional dependency changes.

`msvc-runtime/` contains the app-local x64 `msvcp140.dll`, `vcruntime140.dll`
and `vcruntime140_1.dll` from the user's installed iPhoneMirror v1.8.3 Wireless
package. All three Microsoft signatures were verified Valid during import.
They are required by the upstream AirPlayServer DLL, even though the bridge
and its host use static CRT. Their hashes are in `pins.json`.
Microsoft deployment documentation and license terms:
<https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files>,
<https://visualstudio.microsoft.com/license-terms/>.

The integration does not install Bonjour, change USB drivers, or automatically
modify firewall rules. Network discovery uses the Windows DNS-SD API.
