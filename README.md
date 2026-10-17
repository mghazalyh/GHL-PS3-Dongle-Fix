# GHL PS4 receiver — standalone PS3 EBOOT preflight (v0.7.2)

**Recommended latest release:** [Download preflight v0.7.2 PS3 PKG from GitHub Actions](https://github.com/mghazalyh/GHL-PS3-Dongle-Fix/actions/runs/39040653450/artifacts/12103892260).

Install `ghl_ps4_probe.pkg` on PS3 Evilnat 4.92 CFW using XMB Package Manager. **You do NOT have to replace your game's EBOOT** to run this. The app checks USB receiver 1430:07BB, interface shape the real GHL EBOOT expects, SET_CONFIGURATION, interrupt IN+OUT pipes, two PS4 SET_REPORT keepalive acknowledgments, live receiver `isConnected` indication, and twelve guided guitar-action stages. It saves a conservative preflight verdict on screen and in `summary.txt`, with raw `telemetry.jsonl` available from `http://<your-ps3-ip>:8895/` while the app is running (trusted local network only).

**Black-screen safeguards:** v0.7.2 displays a first startup frame **before** network/USB initialization, then updates an animated `APP ALIVE` indicator. If custom graphics fail, the app attempts a PS3-native system dialog. To **skip custom graphics entirely**, hold **L1+R1** on a normal PS3 controller while launching the app; an on-screen safe-mode system dialog reports USB status. No screenshot/hardware proof yet.

**Passing this app is evidence that a controlled patched-EBOOT trial may be worthwhile, NOT proof of in-game compatibility.** A failed standalone USB claim is not definitive proof that the game's own process cannot claim the device.

[**One-install test instructions and verdict guide**](docs/PS3_EBOOT_PREFLIGHT.md).

---

# Guitar Hero Live PS4 receiver diagnostic for PS3 CFW

Version v0.6-onepass: RSX fullscreen status and raw USB telemetry. EXPERIMENTAL; NOT hardware-tested. This does not make the PS4 guitar work in-game.

## PS3 live display

Updates approximately every 450 ms and shows:
- USB RECEIVER DETECTED only after 1430:07BB extra-LDD attach callback.
- USB INTERFACE READY only after descriptors, USB control pipe and interrupt-IN pipe are usable (not proof of SET_CONFIGURATION).
- PS4 INIT ACKNOWLEDGED only after HID SET_REPORT output 30 02 08 0A 00 00 00 00 00 completes successfully (does not prove radio pairing).
- GUITAR INPUT: successful nonempty USB interrupt reports. Distinguishes report count and raw report changes. Neither proves a paired guitar.
- Live report bytes, total reports, changes, errors, last operation/error code, and next test prompt.

The 180-second guided test does not start until a receiver is attached, required USB pipes exist, initialization completed without error, and at least one nonempty input report has arrived. When any prerequisite is missing, the clock is paused and a reason is displayed. If RSX graphics initialization fails, the app attempts a simpler live PS3 system dialog as fallback.

## Install and run

1. Under Actions, download the latest successful build artifact named GHL-PS3-OnePass-v0.6-PKG.
2. Extract the artifact ZIP. Copy ghl_ps4_probe.pkg to a FAT32 USB drive.
3. On PS3 CFW, open XMB > Package Manager > Install Package Files. Install and launch GHL PS4 USB Diagnostic.
4. Plug the blue-button PS4 GHL USB receiver (VID 1430, PID 07BB) and pair the powered six-fret guitar.
5. Follow the 12 stages of 15 seconds each: black frets 1-3 separately; white frets 1-3 separately; strum up; strum down; chords+strum; whammy; tilt; Hero Power/menu.
6. Use START on a STANDARD PS3 controller to end early, and press START again after releasing the button to dismiss the final onscreen summary.

## Telemetry

Main log: /dev_hdd0/tmp/GHLPROBE/telemetry.jsonl
Main summary: /dev_hdd0/tmp/GHLPROBE/summary.txt
Fallback: /dev_hdd0/tmp/ghl_probe_telemetry.jsonl and /dev_hdd0/tmp/ghl_probe_summary.txt

Collect both original files using FTP or a file manager; report visible error codes and whether input changes correlate with physical fret presses. Files must never be mistaken for proof that the game accepts this dongle.

## Build

The public GitHub Actions workflow downloads a pinned PS3DEV Linux SDK, compiles PowerPC source with PSL1GHT, links RSX video, and creates and validates a PS3 NPDRM PKG. Every push to main triggers a clean rebuild and a fresh artifact. The GitHub free tier is sufficient.

## Attribution and license

The framebuffer helper source/rsxutil.c and source/rsxutil.h derive from ps3dev/PSL1GHT samples/graphics/videoTest, licensed GPL version 3. Public source: https://github.com/ps3dev/PSL1GHT
Source font8x8_basic.h is from Daniel Hepper's public-domain font8x8 repository: https://github.com/dhepper/font8x8
All original project application code is distributed under GPL-3.0-or-later, compatible with the renderer helper. Keep the original license notices.

## Important unresolved questions

Standalone PS3 apps have not been proven able to claim this HID device against system drivers. SET_REPORT acknowledgment is not equivalent to successful radio handshake. PS3 Guitar Hero Live's import/stub vs syscall behavior cannot be decided without inspecting its real EBOOT. The next step after the diagnostic is a hardware test and EBOOT import dump, not a guessed VID/PID patch.

See docs/HARDWARE_VALIDATION.md for a staged fault-isolation plan.

## One-install read-only LAN log export

The app includes a read-only HTTP service based on PSL1GHT networking. Its address appears in green near the bottom of the PS3 dashboard after startup. On a computer or phone on the **same trusted LAN**, open `http://<PS3_IP>:8895/` in a browser while the diagnostic is open; download `telemetry.jsonl` and `summary.txt` after capture. The service is **unauthenticated and unencrypted**: never expose port 8895 to the internet or port-forward it. If the HTTP service fails, the disk files and XMB/FTP recovery method remain available.

The diagnostic allocates USB transfer buffers via USBD, attempts standard SET_CONFIGURATION before starting HID/interrupt traffic, iterates multiple interfaces and endpoints and records detailed stage-by-stage byte-change histograms. The raw reports and histograms cannot prove guitar pairing on their own.

## Automated offline analysis

On the downloaded raw log run `python3 tools/analyze_telemetry.py telemetry.jsonl`. The resulting `telemetry.analysis.json` contains every guided stage's changed-byte counts and classifies unconfirmed steps without guessing button mapping.

See `docs/PRIOR_ART_AND_GAPS.md` for Linux hid-sony, GHLPokeMachine, RPCS3 GHLtar and the proven PS3 PDP Riffmaster project's applicable lessons, and for remaining limitations.
