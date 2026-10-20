#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ ${MSYSTEM:-} == UCRT64 ]] || { echo 'Use MSYS2 UCRT64.' >&2; exit 1; }
pkg-config --exists libimobiledevice-1.0 libplist-2.0 openssl libavcodec libavutil
vendor="$PWD/third_party/rplayhub"
lwip="$vendor/deps/lwip"
if [[ ! -f "$lwip/src/core/init.c" ]]; then
    mkdir -p "$lwip" build/coredevice
    curl -fL --retry 2 https://codeload.github.com/lwip-tcpip/lwip/tar.gz/refs/tags/STABLE-2_2_1_RELEASE -o build/coredevice/lwip.tar.gz
    tar -xzf build/coredevice/lwip.tar.gz --strip-components=1 -C "$lwip"
fi
out="$PWD/build/coredevice-runtime"
mkdir -p "$out/licenses" build/coredevice
flags=(-std=c11 -O2 -Wall -Wextra -DHAVE_USERNET -I"$vendor/core" -I"$vendor/host-c"
    -I"$vendor/host-c/lwip-engine" -I"$lwip/src/include" -I"$lwip/contrib/ports/unix/port/include"
    -I"$PWD/src/AudioCore/src")
read -ra packages <<< "$(pkg-config --cflags libimobiledevice-1.0 libplist-2.0 openssl libavcodec libavutil)"
flags+=("${packages[@]}")
gcc -DHAVE_USERNET -I"$vendor/core" "$vendor/host-c/test_ddi_discovery.c" "$vendor/core/rp_xpc.c" \
    -lws2_32 -o build/coredevice/ddi-discovery-tests.exe
build/coredevice/ddi-discovery-tests.exe
TEST_DDI_ALREADY=1 build/coredevice/ddi-discovery-tests.exe
sources=(src/AudioBridge.CoreDeviceHost/main.c src/AudioBridge.CoreDeviceHost/Decoder.c)
for name in ddi imd media usernet tunio tls_openssl; do sources+=("$vendor/host-c/$name.c"); done
for name in xpc http2 remotexpc coredevice media_offer rtp rtp_assembler rtcp; do sources+=("$vendor/core/rp_$name.c"); done
for name in init def inet_chksum ip mem memp netif pbuf tcp tcp_in tcp_out timeouts udp sys stats; do sources+=("$lwip/src/core/$name.c"); done
for name in ip6 ip6_addr icmp6 nd6 ip6_frag inet6 mld6; do sources+=("$lwip/src/core/ipv6/$name.c"); done
for name in api_lib api_msg netbuf sockets tcpip err; do sources+=("$lwip/src/api/$name.c"); done
sources+=("$vendor/host-c/lwip-engine/sys_arch.c")
gcc "${flags[@]}" -Dmain=cdhost_main -c "$vendor/host-c/cdhost.c" -o build/coredevice/cdhost.o
gcc "${flags[@]}" "${sources[@]}" build/coredevice/cdhost.o \
    $(pkg-config --libs libimobiledevice-1.0 libplist-2.0 openssl libavcodec libavutil) \
    -lwinpthread -lws2_32 -lz -municode -o "$out/AudioBridge.CoreDeviceHost.exe"
gcc -O2 "$vendor/host-c/reveal_developer_mode.c" $(pkg-config --cflags --libs libimobiledevice-1.0 libplist-2.0) \
    -lws2_32 -o "$out/reveal-developer-mode.exe"
queue=("$out/AudioBridge.CoreDeviceHost.exe" "$out/reveal-developer-mode.exe")
declare -A copied=()
for ((i=0;i<${#queue[@]};++i)); do
    while read -r dll; do
        if [[ -f /ucrt64/bin/$dll && -z ${copied[$dll]:-} ]]; then
            cp "/ucrt64/bin/$dll" "$out/";copied[$dll]=1;queue+=("$out/$dll")
            package=$(pacman -Qoq "/ucrt64/bin/$dll")
            license=${package#mingw-w64-ucrt-x86_64-}
            if [[ -d /ucrt64/share/licenses/$license ]]; then cp -r "/ucrt64/share/licenses/$license" "$out/licenses/"; fi
        fi
    done < <(objdump -p "${queue[i]}" | sed -n 's/^[[:space:]]*DLL Name: //p' | tr -d '\r')
done
cp "$vendor/LICENSE" "$out/licenses/rplayhub-GPL-3.0.txt"
cp "$vendor/PROVENANCE.md" "$out/licenses/rplayhub-PROVENANCE.md"
cp "$lwip/COPYING" "$out/licenses/lwip-COPYING.txt"
cp scripts/coredevice/* "$out/"
"$out/AudioBridge.CoreDeviceHost.exe" --check-runtime
echo "CoreDevice runtime: $(cygpath -w "$out")"
