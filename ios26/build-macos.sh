#!/bin/bash
set -euo pipefail
root=$(pwd)
export XNU_AVAILABILITY_PL="$root/availability26/availability"
deps="$root/out/deps"
mkdir -p "$deps/usr/local/include" "$deps/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders"
git clone --depth 1 --branch libplatform-375.120.2 https://github.com/apple-oss-distributions/libplatform.git libplatform26
test "$(git -C libplatform26 rev-parse HEAD)" = b7ed7cf5cf7dd12b98672435db2225a860f199d8
git clone --depth 1 --branch libdispatch-1542.100.32 https://github.com/apple-oss-distributions/libdispatch.git libdispatch26
test "$(git -C libdispatch26 rev-parse HEAD)" = 701f4d1a24ae9c6863901bbbb22624b7d1b87321
git clone --depth 1 --branch xnu-8796.101.5 --filter=blob:none --sparse https://github.com/apple-oss-distributions/xnu.git trustcache-ref
test "$(git -C trustcache-ref rev-parse HEAD)" = aca3beaa3dfbd42498b42c5e5ce20a938e6554e5
git -C trustcache-ref sparse-checkout set EXTERNAL_HEADERS/TrustCache
cp -R trustcache-ref/EXTERNAL_HEADERS/TrustCache kernel26/EXTERNAL_HEADERS/
cp -R libplatform26/include/. "$deps/usr/local/include/"
cp -R libplatform26/private/. "$deps/usr/local/include/"
cp -R libplatform26/private/platform "$deps/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/"
mkdir -p "$deps/usr/local/include/os"
for header in libdispatch26/os/*.h; do
  status=0
  unifdef -DKERNEL=1 -DOS_FIREHOSE_SPI=1 -DOS_VOUCHER_ACTIVITY_SPI_TYPES=1 \
    -UOS_VOUCHER_ACTIVITY_SPI "$header" > "$deps/usr/local/include/os/$(basename "$header")" || status=$?
  test "$status" -le 1
done
kernel_args=(SDKROOT=macosx KDKROOT="$root/kdk-expanded/KDK.pkg/Payload"
  ONYX_DEPS_ROOT="$deps" TARGET_CONFIGS='DEVELOPMENT ARM64 BCM2837'
  PLATFORM=iPhoneOS PLATFORM_FOR_CONFIG=iPhoneOS SDKVERSION=26.5
  RC_DARWIN_KERNEL_VERSION=25.5.0 BUILD_LTO=0 BUILD_WERROR=0 CWARNFLAGS=-w CXXWARNFLAGS=-w
  OBJROOT="$root/out/obj" SYMROOT="$root/out/sym" DSTROOT="$deps")
make -C kernel26 "${kernel_args[@]}" -j3 installhdrs
# Build the real firehose source for the kernel target; no mismatched prebuilt archive.
export ONYX_DEPS_ROOT="$deps"
python3 - <<'PY'
from pathlib import Path
p=Path('libdispatch26/xcodeconfig/libfirehose_kernel.xcconfig')
s=p.read_text().replace('$(SDKROOT)/System/Library/Frameworks/Kernel.framework','$(ONYX_DEPS_ROOT)/System/Library/Frameworks/Kernel.framework').replace('$(SDKROOT)/usr/local/include','$(ONYX_DEPS_ROOT)/usr/local/include')
lines=s.splitlines()
for n,line in enumerate(lines):
    if line.startswith('HEADER_SEARCH_PATHS ='):
        lines[n] = ('HEADER_SEARCH_PATHS = $(PROJECT_DIR) '
          '$(ONYX_DEPS_ROOT)/System/Library/Frameworks/Kernel.framework/PrivateHeaders '
          '$(ONYX_DEPS_ROOT)/System/Library/Frameworks/Kernel.framework/Headers '
          '$(ONYX_DEPS_ROOT)/usr/local/include/kernel '
          '$(ONYX_DEPS_ROOT)/usr/local/include/os '
          '$(ONYX_DEPS_ROOT)/usr/local/include/firehose '
          '$(PROJECT_DIR)/../kernel26/EXTERNAL_HEADERS '
          '$(ONYX_DEPS_ROOT)/usr/local/include $(SDKROOT)/usr/include')
s='\n'.join(lines)+'\n'
p.write_text(s)
PY
xcodebuild install -project libdispatch26/libdispatch.xcodeproj -target libfirehose_kernel \
  -configuration Release -sdk iphoneos -arch arm64 IPHONEOS_DEPLOYMENT_TARGET=26.5 \
  CODE_SIGNING_ALLOWED=NO GCC_TREAT_WARNINGS_AS_ERRORS=NO \
  OBJROOT="$root/out/firehose-obj" SYMROOT="$root/out/firehose-sym" DSTROOT="$deps" \
  ONYX_DEPS_ROOT="$deps" OTHER_CFLAGS='$(inherited) -mcpu=generic -march=armv8-a+crypto'
if [ -f "$deps/usr/local/lib/kernel/liblibfirehose_kernel.a" ]; then
  cp "$deps/usr/local/lib/kernel/liblibfirehose_kernel.a" "$deps/usr/local/lib/kernel/libfirehose_kernel.a"
fi
test -s "$deps/usr/local/lib/kernel/libfirehose_kernel.a"
make -C kernel26 "${kernel_args[@]}" -j3 build
