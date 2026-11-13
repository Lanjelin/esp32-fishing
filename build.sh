#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
arduino-cli compile --clean --fqbn esp32:esp32:lilygo_t_display \
  --libraries "$PWD/libraries" \
  --build-property "build.extra_flags=-DCORE_DEBUG_LEVEL=3 -ffile-prefix-map=$HOME=/build -ffile-prefix-map=/$HOME=/build -ffile-prefix-map=$PWD=. -ffile-prefix-map=/$PWD=." \
  --output-dir "$PWD/build" TwoButtonPad
