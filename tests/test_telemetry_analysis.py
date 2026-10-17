import json
import tempfile
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from analyze_telemetry import analyze

class AnalyzerTests(unittest.TestCase):
    def test_stage_change_correlations(self):
        events=[
            {"t_ms":1,"event":"attach","stage":"waiting"},
            {"t_ms":2,"event":"set_configuration_complete","stage":"waiting","rc":0},
            {"t_ms":3,"event":"set_report_complete","stage":"waiting","rc":0},
            {"t_ms":100,"event":"interrupt_in","stage":"black_fret_1","rc":0,"hex":"000080"},
            {"t_ms":101,"event":"interrupt_in","stage":"black_fret_1","rc":0,"hex":"010080"},
            {"t_ms":102,"event":"interrupt_in","stage":"black_fret_1","rc":0,"hex":"010080"}
        ]
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"telemetry.jsonl"
            p.write_text("".join(json.dumps(x)+"\n" for x in events))
            x=analyze(p)
            self.assertEqual(x["total_nonempty_input_reports"],3)
            self.assertEqual(x["stage_evidence"]["black_fret_1"]["raw_changes"],1)
            self.assertEqual(x["stage_evidence"]["black_fret_1"]["changed_byte_positions"]["0"],1)
            self.assertFalse(x["session_summary"]["pairing_proven"])
            self.assertTrue(x["session_summary"]["receiver_attach_observed"])
    def test_empty_log_does_not_assert_success(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"empty.jsonl"
            p.write_text("")
            x=analyze(p)
            self.assertEqual(x["total_nonempty_input_reports"],0)
            self.assertFalse(x["session_summary"]["input_stream_observed"])
            self.assertIn("extra-LDD",x["triage"][0])
    def test_corrupt_log_line_count(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"bad.jsonl"
            p.write_text("not json\n")
            self.assertEqual(analyze(p)["invalid_lines"],1)

if __name__=="__main__": unittest.main()
