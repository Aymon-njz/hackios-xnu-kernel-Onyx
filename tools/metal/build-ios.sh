#!/usr/bin/env bash
# Build the pinned Metal baseline on macOS. This does not provide Gen8 support
# or the Mesa AIR compiler, install a plugin, modify root images, or flash a phone.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
metal_source=${1:?Usage: build-ios.sh /path/to/hakios-metal /path/to/iPhoneOS16.5.sdk}
sdk_root=${2:?An iOS SDK path is required}
metal_source=$(cd "$metal_source" && pwd)
sdk_root=$(cd "$sdk_root" && pwd)
expected_revision=9d52b5142542a253b7263a0d7889ad278e5c7693
[[ $(/usr/bin/uname -s) == Darwin ]] || { echo 'This build helper requires macOS.' >&2; exit 1; }
[[ $(git -C "$metal_source" rev-parse HEAD) == "$expected_revision" ]] || {
  echo "Expected hakios-metal revision $expected_revision; leaving this checkout alone." >&2; exit 1;
}
[[ -d "$sdk_root/usr/include" ]] || { echo 'Missing iOS SDK headers.' >&2; exit 1; }
patch_file="$repo_root/tools/metal/clang-compat.patch"
if ! git -C "$metal_source" apply --reverse --check "$patch_file" 2>/dev/null; then
  git -C "$metal_source" apply --check "$patch_file"
  git -C "$metal_source" apply "$patch_file"
fi
printf -v quoted_sdk '%q' "$sdk_root"
compiler="xcrun clang++ -target arm64-apple-ios16.0 -isysroot $quoted_sdk"
make -C "$metal_source" test HAKIOS="$repo_root" MESA=
make -C "$metal_source" ios HAKIOS="$repo_root" SDK="$sdk_root" MESA= \
  IOS_CXX="$compiler" IOS_AR='xcrun ar rcs'
bundle="$metal_source/out/ios/HakiosMetal.bundle"
mkdir -p "$bundle"
xcrun clang -target arm64-apple-ios16.0 -isysroot "$sdk_root" -O1 -bundle \
  -framework Foundation -framework Metal -framework IOKit -framework IOSurface \
  -lobjc -lc++ -DHAKIOS_METAL_GPU -I"$metal_source/include" -I"$metal_source/plugin" \
  -o "$bundle/HakiosMetal" "$metal_source/plugin/HakiosMetal.m" \
  "$metal_source/plugin/hakios_metallib.c" "$metal_source/out/ios/libhakios-metal.a"
cp "$repo_root/userland/src/HakiosMetal.plist" "$bundle/Info.plist"
plutil -lint "$bundle/Info.plist"
xcrun vtool -show-build "$metal_source/out/ios/hakios-metal-probe"
xcrun vtool -show-build "$bundle/HakiosMetal"
shasum -a 256 "$metal_source/out/ios/libhakios-metal.a" \
  "$metal_source/out/ios/hakios-metal-probe" "$bundle/HakiosMetal"
