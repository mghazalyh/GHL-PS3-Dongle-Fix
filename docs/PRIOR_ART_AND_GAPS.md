# USB compatibility evidence review — GHL PS4 receiver on PS3 CFW

Snapshot: 17 October 2026. Reviewed sources below against our implementation.
The physical PS3 dongle `1430:07BB` has NOT been tested with this app. Compilation is not hardware proof.

## What past projects establish

| Prior implementation | Grounded evidence | Reused/validated technique | What does NOT transfer |
| --- | --- | --- | --- |
| Linux `hid-sony.c` (torvalds/linux) | PS4 GHL keepalive data `30 02 08 0A 00 00 00 00 00`; control setup `0x21/0x09/wValue=0x0230/wIndex=0/length=9`; 8-second interval | Our app sends same HID output class control request every 7 sec with callback reporting | Linux host has an owning HID driver; PS3 GameOS may not let a standalone process claim the receiver |
| Octave13/GHLPokeMachine | Uses `HidD_SetOutputReport` with PS4 report, HID descriptor enumeration | Compare status/output length, log submit and completion independently | Windows HID output-report API isn't equal to PS3 GameOS's device-ownership model |
| alansouls/ps3-pdp-plugin | Hardware-tested on Evilnat Cobra for other GH/RB titles; game-process extra-LDD, `cellUsbdAllocateMemory`, `cellUsbdSetConfiguration` then interrupt endpoint, walks all USB interfaces; includes Cobra/PS3MAPI game injection | This diagnostic now mirrors DMA allocation, configuration completion, walking endpoints, callback-specific errors | Different physical guitar: PDP Riffmaster PS4 uses `0E6F:024A`; source hooks `cellPad` in titles tested, not GHL PS3 USB instrument path |
| RPCS3 `GHLtar.cpp` | Virtual PS3 receiver `12BA:074B`, HID interface with EP `0x81` and `0x01`, 32-byte endpoint max packet; expected 27-byte report (six frets, strum, whammy, tilt, hero power) | Reference for future PS4-to-PS3 input translation after measured 1430:07BB payloads; protocol baseline | RPCS3's virtual USB device doesn't establish how a physical PS3 game/USB stack can present spoofed descriptors |
| RPCS3 `sys_usbd.cpp` | Models low-level `sys_usbd` peripheral detection separate from ordinary `cellPad` | Plan must consider USB descriptors and direct low-level access, not just virtual pads | RPCS3's cellUsbd high-level wrappers are still largely unimplemented, no guarantee GHL game uses them |
| PS3xPAD / PS3 PDP virtual pads | Plugins can expose modern controllers via `cellPad` to games that read `cellPad` | Future process-injection/CFW design reference only | No proof GHL Live PS3 accepts ordinary `cellPad` in place of genuine `12BA:074B` dongle |

## Current one-install diagnostic capture

- The app is a **separate** PS3 homebrew application (not in-game plugin).
- LDD registration for `1430:07BB`; all statuses and failures saved.
- Walk all interfaces/endpoints, log raw HID descriptor and USB device descriptors; accept host-swapped/wire-LE VID/PID representation on PowerPC.
- Allocate USB buffers via PSL1GHT `usbAllocateMemory`, send standard `SET_CONFIGURATION` request and wait for completion, then open interrupt-IN endpoint and send Linux-equivalent PS4 keepalive.
- Record asynchronous transfer completion codes, timeout flags, input bytes, raw report change counts and per-guided-stage byte-change histogram.
- Show live RSX framebuffer with receiver/USB configuration/interface/PS4 init/input statuses. None of these alone confirms guitar pairing.
- Read-only HTTP export `http://<PS3_IP>:8895/` on the LAN offers raw JSONL and summary without FTP. Logs remain on disk if LAN isn't available. **No authentication or TLS**: trusted LAN only, never port-forward.
- Includes an offline `tools/analyze_telemetry.py` script and host synthetic tests.
- Final summary stays on screen until a separate START press. The guided clock waits until the dongle, USB config, HID ACK and at least one nonempty report are verified.

## Remaining gating unknowns

1. **Extra-LDD ownership**: A successful callback with PDP Riffmaster in a game plugin does not prove a standalone app may own PS4 GHL's 1430:07BB HID device. If no attach, investigate system driver claim/priority or a more privileged VSH/game-process approach.
2. **PS3 compatibility of class control request**: Linux/Windows establish correct packet but not GameOS transport behavior. We now log actual configuration and SET_REPORT completions separately, exposing failures.
3. **Raw PS4 controller report format**: Must be measured against a paired guitar. Decode and map against RPCS3's PS3 GHL report reference. Stage-by-stage byte-change histogram allows data analysis without separate tests for each button.
4. **Game integration**: We DO NOT yet know whether BLUS31556/BLES02180 calls high-level `cellUsbd` imports, invokes `sys_usbd` directly or uses additional integrity/configuration checks. Dump imports of the *actual* game's EBOOT (including an installed update EBOOT if present) before selecting EBOOT patch vs in-game SPRX.
5. **Full game compatibility**: Requires successfully presenting virtual USB device identity, descriptors, reports and control behavior to *Guitar Hero Live*, not just decoding input through cellPad.

No honest numeric success probability is available without testing the physical PS3.

## Source links

- https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c
- https://github.com/Octave13/GHLPokeMachine/blob/master/GHL_Library/GHLPoke.cpp
- https://github.com/alansouls/ps3-pdp-plugin/blob/main/ps3-plugin/README.md
- https://github.com/alansouls/ps3-pdp-plugin/blob/main/ps3-plugin/game/riffmaster_game.c
- https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Io/GHLtar.cpp
- https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/lv2/sys_usbd.cpp
- https://github.com/aldostools/PS3xPAD
