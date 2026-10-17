# GHL PS4-to-PS3 USB bridge: source audit and implementation gate

Status: OFFLINE BRIDGE PROTOTYPE. No in-game SPRX has been built or hardware tested.
Target: Evilnat CFW 4.92 with Cobra; GHL PS3 EBOOT; PS4 dongle 1430:07BB.
DO NOT put copyrighted decrypted EBOOT files in this public repository.

## EBOOT disassembly: what is supported

- LDD registration at 0x00C58AF8 calls cellUsbdRegisterExtraLdd2 with ops 0x014B705C, VID 0x12BA, PID range 0x0100..0x074B.
- Probe 0x00C59498 / attach 0x00C59698 check HID descriptors, including one interface and exactly two interrupt endpoints IN and OUT. We have NOT confirmed the physical PS4 dongle has a matching layout.
- Interrupt IN call at 0x00C587F8 receives into ctx+0x314 (max requested 256), callback at 0x00C5890C copies into ctx+0x414 and sets ctx+0x518 to completion byte count.
- Dispatcher at 0x00C578B0 includes a conditional 27-byte length check, change detection, and forwarding to additional handler objects.
- Game's HID SET_REPORT at 0x00C587B4 has wValue 0x0201; PS4 receiver needs independent HID output report 0x30 (0x21/0x09/wValue 0x0230 and 9-byte magic).
- Import-table handling and TOC semantics must be established before any game memory writes.

These statements describe and critically assess user-supplied disassembly, not our own independent decryption of a binary.

## Source-backed input translation, implemented

bridge/ghl_translate.c offers ghl_ps4_to_ps3(): from a PS4 1430:07BB raw report (report ID 1, minimum 8 bytes) to exactly 27 bytes of PS3 format. The mapping comes from public PlasticBand 6-fret instrument specifications and GHLtarUtility's PS3 implementation.

| Input | PS4 raw | PS3 destination |
| --- | --- | --- |
| White 1 | byte5 bit4 | byte0 bit0 |
| Black 1 | byte5 bit5 | byte0 bit1 |
| Black 2 | byte5 bit6 | byte0 bit2 |
| Black 3 | byte5 bit7 | byte0 bit3 |
| White 2 | byte6 bit0 | byte0 bit4 |
| White 3 | byte6 bit1 | byte0 bit5 |
| Pause | byte6 bit5 | byte1 bit1 |
| GHTV | byte6 bit6 | byte1 bit2 |
| Hero Power | byte6 bit7 | byte1 bit0 |
| PS/dpad center | byte7 bit0 | byte1 bit4 |
| D-pad | byte5 low nibble, neutral 8 | byte2, neutral 15 |
| Strum up/center/down | byte2, values 0/128/255 | byte4 |
| Whammy | byte4 | byte6 |
| Tilt | byte3, nominal center 128 | bytes19..20, 10-bit provisional scaling |

Tilt conversion remains provisional until live input and the game's downstream handler are compared. This mapping does not infer successful guitar pairing from idle reports.

References:
- https://github.com/TheNathannator/PlasticBand/blob/main/Docs/Instruments/6-Fret%20Guitar/PS4.md
- https://github.com/TheNathannator/PlasticBand/blob/main/Docs/Instruments/6-Fret%20Guitar/PS3%20and%20Wii%20U.md
- https://github.com/TheNathannator/PlasticBand/blob/main/Docs/Base%20Reports/PS4.md
- https://github.com/TheNathannator/PlasticBand/blob/main/Docs/Base%20Reports/PS3.md
- https://github.com/ghlre/GHLtarUtility/blob/master/PS3Guitar.cs

## Correct four critical errors before building an in-game hook

1. **PS3 USB API signatures:** PSL1GHT declares five arguments for cellUsbdInterruptTransfer(pipe,buf,len,cb,arg) and cellUsbdControlTransfer(pipe,setup,buf,cb,arg). DeepSeek's example has six; copying it directly risks a crash. The async callback itself is callback(result,count,arg).
2. **Patchable import location and PPC64 TOC:** The supplied report calls the addresses 0x012662D4, 0x01266194 and 0x012662B4 eight-byte function descriptors, while the same report shows direct 'bl' instructions targeting them. A direct branch cannot execute a literal {entry,TOC} data pair. Verify exact instruction bytes/actual import veneer, patch target, segment permissions, TOC and required cache operations. Do not blindly write a descriptor or new code pointer.
3. **Async completion count and lifetime:** When a 64-byte PS4 frame becomes a 27-byte PS3 frame, forward result=0,count=27 to the original callback; otherwise the game copies or dispatches the wrong number of bytes. Never complete a request twice, reuse DMA memory before completion or leave timers active through detach. Suppression/conversion of PS3 SET_REPORT must preserve its original completion contract.
4. **Injection timing and descriptor mismatch:** The proven Riffmaster loader injects eight seconds after finding a running game, which may be too late for GHL USB registration. Also GHL attach requires two interrupt endpoints; actual PS4 descriptor match is still unconfirmed. Do not assume changing VID/PID alone is sufficient.

## Development stages

- Complete: source-supplied EBOOT USB call audit; public PS4/PS3 report spec comparison; portable C translator; synthetic compile/test in GitHub Actions.
- Next: obtain evidence for actual import-stub bytes and confirm early-loading strategy. Extract USB endpoint descriptors from a PC or single PS3 run; avoid extra console installs.
- Then: implement Cobra game-process bridge with exact game-build fingerprint, report translation callback, independent PS4 keepalive, async transfer ownership, descriptor fallback and fail-open bypass.
- Only after hardware-independent checks pass: build ONE candidate for physical PS3 test with visible status + downloadable logs.

Offline tests: .github/workflows/bridge-tests.yml (no package upload/installation).
PS3 diagnostic PKG workflow now builds on source changes, not every research/documentation commit.

Prior implementation for Cobra process injection: https://github.com/alansouls/ps3-pdp-plugin
Known PS4 keepalive: https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c
These references establish techniques, not physical PS3 GHL interoperability.
