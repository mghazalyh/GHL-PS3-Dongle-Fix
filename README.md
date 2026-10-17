# GHL PS4 dongle USB diagnostics on PS3 CFW

**Experimental diagnostic; not a game compatibility patch. Not hardware-tested.**

Collects USB descriptors, asynchronous HID SET_REPORT responses and raw interrupt input reports from the blue-sync PS4 Guitar Hero Live receiver (USB 1430:07bb). Pair guitar and manipulate six frets, strum, whammy and tilt over 180 seconds. Run this separately from Guitar Hero Live.

To build: install PS3DEV Linux SDK, then `make -f Makefile.ps3dev`. CI fetches a pinned public PS3DEV release and builds a fresh NPDRM .pkg; download from the Actions run artifact, after checking that the build succeeded.

Install resulting `ghl_ps4_probe.pkg` using the PS3 CFW Package Manager.

Log files:
- `/dev_hdd0/tmp/GHLPROBE/telemetry.jsonl`
- `/dev_hdd0/tmp/GHLPROBE/summary.txt`

Fallback: `/dev_hdd0/tmp/ghl_probe_telemetry.jsonl` and `/dev_hdd0/tmp/ghl_probe_summary.txt`.

The probe does not modify game files and cannot by itself confirm whether the game can accept the PS4 dongle. Source and resulting software are experimental; game and USB hardware compatibility remain to be determined.
