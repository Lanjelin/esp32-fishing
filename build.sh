#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
arduino-cli compile --clean --fqbn esp32:esp32:lilygo_t_display \
  --libraries "$PWD/libraries" \
  --build-property "build.extra_flags=-DCORE_DEBUG_LEVEL=3 -ffile-prefix-map=$HOME=/build -ffile-prefix-map=/$HOME=/build -ffile-prefix-map=$PWD=. -ffile-prefix-map=/$PWD=." \
  --output-dir "$PWD/build" TwoButtonPad

# Keep the checked-in web installer synchronized with every successful build.
firmware_name=TwoButtonPad-t-display-v1.1.bin
firmware_hash=$(sha256sum build/TwoButtonPad.ino.merged.bin)
firmware_hash=${firmware_hash%% *}
sed -E "s|firmware/${firmware_name}\\?sha256=[0-9a-f]{64}|firmware/${firmware_name}?sha256=${firmware_hash}|" \
  docs/manifest.json > build/manifest.json
cp build/TwoButtonPad.ino.merged.bin "docs/firmware/$firmware_name"
printf '%s  %s\n' "$firmware_hash" "$firmware_name" > docs/firmware/SHA256SUMS
cp build/manifest.json docs/manifest.json
