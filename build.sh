#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
arduino-cli compile --fqbn esp32:esp32:lilygo_t_display \
  --libraries "$PWD/libraries" \
  --build-property 'build.extra_flags=-DCORE_DEBUG_LEVEL=3' \
  --output-dir "$PWD/build" TwoButtonPad
