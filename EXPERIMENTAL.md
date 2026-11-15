# gone-fishing branch notes

This branch adds on-device audio-assisted fishing to the original LilyGO /
TTGO T-Display v1.1 (ESP32, not S3/C3). The README and flasher copy intentionally
document only manual controls, orientation and the controller's friendly name.
The bundled full-install image is built from this branch. These notes cover the
additional behaviour visible in the source, not advertised in the README.

## Controls and Bluetooth

- Controller: **Desk Timer**. Audio sink: **Desk Audio**.
- The tested Xbox HID descriptor, PnP IDs and gamepad appearance are retained.
  Friendly names do not conceal the advertised device class.
- The user verified iPhone / GeForce NOW recognition after initial re-pairing:
  `GamePad 1 connected: XInput Controller #1`. The Xbox friendly name is not
  required with the current HID/security configuration. The earlier failure's
  cause was not isolated because name/security fixes originally changed together.
- Cast stays above Reel in either orientation; physical roles and highlights
  follow the orientation. Normal Cast/Reel taps are resolved on release.
- Hold **Reel for two seconds** to rotate 180 degrees. The orientation is saved.
  The gesture consumes the held button until release, without sending a tap.
- Hold **Cast for two seconds** to enter/exit audio mode. Entering does not cast.
  Select Desk Audio as the phone's audio output; the ESP consumes it silently
  and does not relay it to speakers or headphones.
- In audio mode, short-press Reel to Start/Stop. Both-button desk-timer switching
  remains available in normal mode, not audio mode. The saved timer-screen
  preference is retained.
- Start requires a controller connection, incoming audio and the reference bank.
  Stop, mode exit, disconnect, audio loss or overflow stop the loop. It never
  resumes automatically after reboot or reconnection.

## Detection and timing

The original sound files are downloaded and SHA-256 verified on the build host;
only compact normalized five-slice, 16-band features are embedded in firmware.
No live recording is learned, embedded or uploaded during normal operation.

| FileDataID | Original reference |
| --- | --- |
| 569285 | FishingBobber_ver2_1 |
| 568970 | FishingBobber_ver2_2 |
| 569044 | FishingBobber_ver2_3 |
| 569816 | Classic FishBite |

The first three come from [live Fishing Hooked](https://www.wowhead.com/sound=3355/fishing-hooked);
FishBite comes from [Classic Fishing Hooked](https://www.wowhead.com/classic/sound=3355/fishing-hooked).
The latter closely matched a manually confirmed missed bite in WoW Forever.
Pinned URLs, sizes and hashes are in `tools/bite-reference-sources.json`.
Raw Blizzard audio stays in ignored build cache and is not redistributed under
this project's MIT license.

`BiteTemplates.h` contains **120 signatures**: four sources, five bounded
speed/pitch factors (0.95, 0.975, 1, 1.025, 1.05), and six frame alignments.
Generation normalizes to 44.1 kHz before changing playback rate; some original
assets are 22.05 kHz. Runtime filtering/resampling produces 11025 Hz analysis.
Strict gates remain spectral 0.96, envelope 0.95 and relative contrast 1.6.

- Ignore matches for 1.5 seconds after Cast release to suppress landing sounds.
- After an accepted match, wait a latched random **150–300 ms**, then send Reel.
  This is after detection, not sound onset; buffering/analysis adds latency.
- Cast/Reel pulses have a random 150–210 ms minimum from successful submission.
- Wait **one second after submitting Reel release** before casting again.
- After **30 seconds without a bite**, count a timeout and recast.

Timing values live in `AutoFishing.h` / `InputRouter.h`; main-loop randomization
uses the same bounds. Processing is non-blocking except bounded UI/serial work.
A2DP callbacks only queue PCM; the main loop runs the detector. Audio overflow
stops the run rather than continuing with a damaged stream.

## First-cast amplitude baseline

On each Start, measure the first cast for 1.5 seconds from the first successfully
queued Cast report. Peak energy over a 70 ms slice becomes a RAM-only baseline.
It stays fixed through catches and timeout recasts until Stop. No Bluetooth
volume control or NVS calibration is involved.

A pattern match must also have peak slice RMS at least the cast RMS
(`CastAmplitude::minimumRatio`, currently 1.0). Missing/near-silent calibration
stops the run with `AUTO: cast audio missing`; there is no ungated fallback.
Stop/Start again after changing game volume. Quiet nearby bobbers can be vetoed,
but another player at a similar level may still be indistinguishable.

## Auto status and totals

Running AUTO text is green; stopped/fault text is orange. The same line shows a
right-aligned white `Fish <caught>/<missed>`:

- Caught counts the first successfully queued bite-triggered Reel, **not verified
  loot**. Keepalives and manual controls do not increment it.
- Missed counts only listening timeouts, not manual stops, audio failures or
  disconnects.
- RAM totals survive Stop/Start and mode switching; reboot resets them.
- Display values above 9999 become `9999+`; internal 32-bit totals saturate.
- Status/counter changes redraw only the status strip. Actual Font2 widths fit
  normal status plus maximum compact totals in the 224 px content width in
  either orientation; long fault text takes precedence when needed.

## Build and checks

Requires Arduino CLI, Arduino-ESP32 3.3.10 and M5GFX 0.2.19. This branch uses the
core's Bluedroid BLE + ESP-IDF A2DP APIs, not NimBLE/BleGamepad at runtime. The
vendored gamepad project supplies the tested descriptor; vendor licenses and
baseline dependencies are retained. Classic Bluetooth memory must not be
released during BLE initialization. See `DEPENDENCIES.txt`.

Normal builds use the committed feature bank:

```sh
./build.sh
```

To regenerate and verify references, additionally install ffmpeg and a C++17
compiler, then run:

```sh
python3 tools/build_bite_templates.py
python3 tools/check_bite_templates.py
```

Host checks (outputs stay in ignored build storage):

```sh
mkdir -p build
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/input_router.cpp -o build/test-input
build/test-input
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/auto_fishing.cpp -o build/test-auto
build/test-auto
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/cast_amplitude.cpp -o build/test-amplitude
build/test-amplitude
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/bite_noise.cpp -o build/test-noise
build/test-noise
python3 tests/test_capture_protocol.py
```

Checks cover 169 reference cases (lossy audio, volume/EQ, held-out speed/phase,
reversed-order and synthetic negatives), 512 additional noise clips, 18,422
pulse/delay/rollover cycles, stop/disconnect, counter semantics and calibration.
A broader fallback was rejected despite passing selected examples: it recovered
old candidate clips at only 4/16 and 3/16 frame alignments. Future matcher changes
must test unseen alignment as well as positives, negatives, amplitude and states.

## Installation and diagnostics

Before an application-only update, read the actual partition and OTA selection,
save the installed application and verify the staged image. The tested layout
selects app0 at 0x10000, size 0x140000. Do not assume every board has that layout;
do not erase flash or overwrite NVS/partition metadata to update an application.
Keep pairing and saved display preferences. The web flasher uses this branch's
full merged image at offset zero. Unlike an application-only update, that full
image resets pairings and display preferences.

Serial at 115200:

- `b`: bond/authentication/subscription status, without keys or peer addresses.
- `v`: toggle per-second diagnostic summaries and dry-run pattern matches.
- `a`: bounded 40-second post-filter PCM export; requires audio mode/streaming.
- `s` / `t`: current UI / desk-timer screenshot. Under Bluetooth memory pressure,
  full-frame export may return `SHOT_ERROR memory` rather than allocating.
- `c`: legacy command; live sound learning remains disabled.

`CAST_BASELINE` reports calibration; `QUIET_BITE` reports amplitude vetoes.
`AUTO` and `HID` distinguish timeout recasts, matched bites and queued inputs.
Queue acceptance is not an acknowledgement of delivery from the phone/game.
Diagnostic export adds processing/serial overhead; do not use it as normal CPU
performance evidence.

The standard-library capture helper supports a Windows SSH-connected COM port:

```sh
python3 tools/capture_audio.py --ssh dev-pc --port COM3 --output build/session.wav
```

An explicit `--allow-missing-prefix` permits only one/two initial 256-sample
packets to be absent, records the gap without padding, and still rejects middle
gaps, incomplete captures and reported overruns. A capture header alone is not
proof that audio packets are arriving. Opening some USB bridges can reset the
board: re-enable/select audio before retrying; never count failed recordings as
usable examples. Keep private recordings/logs out of the repository.

## Validation and remaining limits

Five post-reference-update single attempts were user-confirmed auto catches,
covering all four sources. The manually confirmed missed example and three
other candidate recordings were recovered at all 16 tested frame alignments;
the labelled quiet false trigger was rejected by the amplitude gate. Some older
misses were only visually reported, not confirmed by manual catches: do not
silently turn their strongest candidate sounds into known-positive labels.

The user later reported 20 minutes with no noticed misses at the 150–300 ms
reaction setting, and confirmed the neutral Bluetooth names after re-pairing.
These are user observations, not a measured long-run accuracy guarantee. The
ESP cannot verify loot, identify who made a same-level splash, or guarantee
compatibility with untested clients/boards/audio settings. No broader sensitivity
fallback or persistent learning was enabled. Flasher assets are prepared locally;
signing, pushing, merging and publication remain with the user.
