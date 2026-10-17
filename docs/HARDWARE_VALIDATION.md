# Physical PS3 v0.5 USB live dashboard test plan

Test A: launch with dongle UNPLUGGED. The UI should say WAITING and zero reports. The guided timer must NOT advance. Press START once to end capture; the summary should remain until releasing START and pressing again.

Test B: plug in the PS4 receiver with guitar OFF. Watch receiver detection, USB pipe setup, PS4 SET_REPORT command submission and callback status. Note exact error code. Some receivers send idle packets unpaired: REPORTS ARRIVING is NOT evidence of a guitar pairing.

Test C: power guitar with fresh batteries, pair it and work the guided 12 stages. Observe whether LATEST REPORT bytes change in synchrony with the requested actual fret/strum/tilt/whammy actions. Bytes changing without correlated actions are not pairing proof.

Test D: when finished, the summary should stay on-screen until START is pressed. FTP full telemetry.jsonl and summary.txt from /dev_hdd0/tmp/GHLPROBE/ (or fallback root tmp file paths). Preserve prior files for comparisons.

Fault classification:
- RSX/UI fails: graphics subsystem first; do not classify dongle as bad.
- Receiver not detected: USB ownership and extra-LDD registration may be blocked.
- Receiver detected, no USB endpoints: inspect descriptor/class/interface/endpoint mismatch.
- SET_REPORT submission/completion fails: control request report format/firmware permissions remain unknown.
- Initialization ACK but no raw input: radio pairing, correct endpoint or receiver mode require testing.
- Nonempty raw reports only: do not infer guitar pairing.
- Input reports correlate with frets: collect mappings and inspect PS3 GHL BLUS31556/BLES02180 EBOOT imports. Game patch/SPRX path remains unproven.

Send back: PS3 model/CFW, screenshot of full live dashboard, precise error code, raw telemetry JSONL, summary TXT, dongle LED behavior and whether button movements correlate with report changes.


## Browser instead of FTP

In the **new v0.6** package, open `http://<IP_SHOWN_ON_PS3>:8895/` on another device on the same trusted LAN **while the PS3 diagnostic is running**. Use the two links there to download raw and summary logs. The webserver is read-only and serves no other files. LAN-only; no TLS/authentication, do not port-forward.

Observe separate USB configuration callback status before the output report status. The report counter starts only on nonempty interrupt reads. The stage byte-change summary attempts to correlate user actions with raw bytes, never asserts pairing.
