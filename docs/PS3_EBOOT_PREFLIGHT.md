# PS3 GHL PS4 dongle preflight v0.7.1 — before trying the modified EBOOT

**Standalone PS3 CFW package.** Install this homebrew package on Evilnat 4.92.
It **does not patch or launch Guitar Hero Live**, does not modify the ISO,
and does not require the decrypted EBOOT on the console.

## Purpose: test real PS4 receiver with native PS3 USB APIs once

A successful preflight reduces uncertainty about actual receiver ownership,
configuration, USB endpoints, PS4 initialization, pairing and instrument reports.
It **cannot** verify that the modified EBOOT boots, that GHL uses the same USB
device ownership privileges as homebrew, that the game's HID descriptor callback
accepts the live receiver, or that its periodic output works in-game.
A NO-GO can also mean *standalone app ownership failed*, rather than prove a
game-process hook must fail.

This preflight was designed using the independently disassembled, private GHL
EBOOT (SHA256 `c1817fc3506b776125b6cc81b0f4d035e417a10dc7eda464e8874b4d7b7d785f`)
**without including the copyrighted executable or any keys in this repo**.

## What the LIVE SCREEN distinguishes

- **USB RECEIVER**: matching `1430:07BB` attach + raw descriptor match
- **USB CONFIG**: standard SET_CONFIGURATION callback reported success
- **GHL GAME HID SHAPE**: single-interface configuration with HID class 3,
  interface protocol 0, two interrupt endpoints (one IN, one OUT),
  plus successfully opening required OUT pipe. This models observed GHL
  attach *requirements*, but is not a full emulation of the game's callback
- **PS4 INIT (0x30)**: success for **at least two** 7-second interval HID
  SET_REPORT 0x30 control requests, rather than a single submit or ACK
- **GUITAR INPUT**: USB reports are NOT sufficient by themselves; the PS4
  format's `isConnected` field at report byte 25 must indicate connected
  for **three consecutive** recent valid frames
- **ACTION STAGES**: 12 timed prompts, covering individual six frets, up/down
  strums, chords, whammy, tilt, hero/menu. Counts stage-specific events
  rather than any spontaneous HID bytes
- **ERROR CODE**: last USB operation failure; results and errors persist
  as a summary even after the timed capture

## Step-by-step test

1. Go to the repository's **Actions -> Build PS3 GHL v0.7.1 EBOOT preflight PKG**
   and open the latest **successful** run. Download the artifact named
   `GHL-PS3-EBOOT-Preflight-v0.7.1-PKG`.
2. Extract the ZIP, copy `ghl_ps4_probe.pkg` to a FAT32 USB thumb drive.
3. On your Evilnat 4.92 PS3, use XMB -> Package Manager ->
   Install Package Files. This installs **only** the preflight application.
4. Plug your blue PS4 GHL dongle (1430:07BB) into PS3 USB.
   Power and pair your guitar; keep a **normal PS3 controller** connected
   for START.
5. Launch **GHL PS4 EBOOT Preflight** from XMB. Follow the 12 prompts.
   The guided timer **does not start on idle receiver reports**; it waits
   for genuine PS4 HID reports and a sustained `isConnected=1`.
6. When the 3-minute guided capture finishes, the final verdict remains
   on the screen. If no guitar is ready after 2 minutes, it finishes with
   the reason instead of waiting silently. To finish earlier, press START
   on the normal PS3 controller; release and press START again only when
   ready to leave the summary.
7. Before exiting, from a PC or phone on the same **trusted local network**,
   open the LAN URL shown near the bottom of the PS3 dashboard:
   `http://<YOUR_PS3_IP>:8895/`. Download both `summary.txt` and
   `telemetry.jsonl`. No FTP should be required when this LAN server
   functions. If it does not, the original files remain at
   `/dev_hdd0/tmp/GHLPROBE/summary.txt` and `telemetry.jsonl`.

The LAN server has **no authentication or TLS**. Never port-forward TCP 8895
or use it on an untrusted shared network.

## How to interpret the final verdict

| On-screen verdict | Meaning | Next step |
| --- | --- | --- |
| `PREFLIGHT PASS: TRIAL POSSIBLE` | Device claimed; PS4 HID shape plausible for game; USB configuration, repeated initialization and input confirmed; live connected guitar and >=7 action stages including both strum directions verified, no recorded errors | You may carefully test the **separately backed-up** patched EBOOT (still experimental) |
| `NO-GO: ...` | A specific prerequisite wasn't observed in this standalone application | Download log; do **not** assume changing game EBOOT solves this; inspect exact failure |
| `INCONCLUSIVE: ...` | Not enough correlated controls or intermittent USB errors | Consult logs and user-action stages first; may still be possible to test targeted EBOOT in a controlled way |
| `WAIT: ...` | Check is ongoing and the guided timer has not started | Pair guitar or resolve the indicated missing condition |

Even a PASS **does not prove gameplay** or that a patched EBOOT will boot. The
game's process privileges, control-transfer frequency, descriptor virtualisation,
and report handler behavior remain testable only when the game runs.

## What to share back

Send a photo of the final screen and the two LAN files. The
`summary.txt` now includes current/ever-connected status, complete descriptor
shape decision, multiple keepalive callbacks, valid/invalid PS4 frame counts,
all 12 action stages, report sizes and operation error codes. The original
telemetry JSONL contains full (up to 64-byte) raw reports for follow-up analysis.

GitHub Actions runs host regression tests and cross-compiles a new PS3 NPDRM
package on the free-tier GitHub Actions runners. Compiled successfully
does **not** mean tested on your physical hardware.
