#!/usr/bin/env python3
"""Analyze GHL PS3 receiver telemetry; no external dependencies.
Produces evidence-backed stage/byte change matrix, never asserts pairing.
"""
import argparse
import collections
import json
from pathlib import Path

STAGES = ("black_fret_1","black_fret_2","black_fret_3","white_fret_1",
          "white_fret_2","white_fret_3","strum_up","strum_down",
          "chords_and_strum","whammy","tilt","menu_hero_power")

def analyze(path):
    events = collections.Counter()
    changes = collections.Counter()
    totals = collections.Counter()
    byte_changes = collections.defaultdict(collections.Counter)
    errors = []
    last_by_stage = {}
    first_last = {}
    bad_lines = 0
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            try:
                d = json.loads(line)
            except (ValueError, TypeError):
                bad_lines += 1
                continue
            ev = d.get("event", "")
            stage = d.get("stage", "waiting")
            events[ev] += 1
            if ev == "error":
                errors.append({"line": line_no,"t_ms":d.get("t_ms"),
                               "operation":d.get("operation"),"rc":d.get("rc"),
                               "rc_hex":d.get("rc_hex")})
            if ev == "interrupt_in" and d.get("rc", 1) == 0 and d.get("hex"):
                try:
                    raw = bytes.fromhex(d["hex"])
                except ValueError:
                    bad_lines += 1
                    continue
                totals[stage] += 1
                if stage not in first_last:
                    first_last[stage] = {"first":list(raw[:27])}
                first_last[stage]["last"] = list(raw[:27])
                prev = last_by_stage.get(stage)
                if prev is not None and raw != prev:
                    changes[stage] += 1
                    for i in range(max(len(prev),len(raw))):
                        a = prev[i] if i < len(prev) else None
                        b = raw[i] if i < len(raw) else None
                        if a != b:
                            byte_changes[stage][str(i)] += 1
                last_by_stage[stage] = raw
    detected = events["receiver_detected"] > 0 or events["attach"] > 0
    configured = events["set_configuration_complete"] > 0
    init = events["set_report_complete"] > 0
    reports = sum(totals.values())
    problems = []
    if not detected: problems.append("No PS4 receiver attach observed: USB extra-LDD ownership remains unproven.")
    elif not configured: problems.append("No USB configuration callback: inspect config submission/completion and endpoint ownership.")
    elif not init: problems.append("No HID SET_REPORT completion: inspect request parameters and callback status.")
    elif not reports: problems.append("No successful nonempty USB input reports: investigate input endpoint/poke/pairing.")
    else: problems.append("Raw reports are visible. Correlate stage-specific byte changes with physical actions. Pairing/game support NOT proven.")
    if bad_lines: problems.append("Malformed JSON lines: %d" % bad_lines)
    summary = {
        "origin": str(path),
        "device_target": {"vid": "1430", "pid": "07bb"},
        "events": dict(sorted(events.items())),
        "total_nonempty_input_reports":reports,
        "error_events":errors,
        "invalid_lines":bad_lines,
        "session_summary":{
            "receiver_attach_observed":detected,
            "configuration_callback_observed":configured,
            "set_report_callback_observed":init,
            "input_stream_observed":reports>0,
            "pairing_proven":False,
            "game_compatibility_proven":False
        },
        "stage_evidence":{
            stage: {
                "reports": totals[stage], "raw_changes":changes[stage],
                "changed_byte_positions":dict(sorted(byte_changes[stage].items(),
                              key=lambda kv:int(kv[0]))),
                "samples":first_last.get(stage,{})
            } for stage in STAGES
        },
        "triage":problems
    }
    return summary

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("log", type=Path,help="telemetry.jsonl downloaded from PS3")
    args=parser.parse_args()
    result=analyze(args.log)
    out=args.log.with_suffix(".analysis.json")
    out.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print("Reports:",result["total_nonempty_input_reports"])
    print("Errors:",len(result["error_events"]))
    for finding in result["triage"]:print(" -",finding)
    print("Analysis:",out)

if __name__=="__main__":main()
