import importlib.util
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

spec=importlib.util.spec_from_file_location(
    "eboot_inspect",Path(__file__).resolve().parents[1]/"tools"/"eboot_inspect.py")
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class EbootTriageTests(unittest.TestCase):
    def test_detect_self(self):
        r=module.inspect_blob("PS3_GAME/USRDIR/EBOOT.BIN",b"SCE\x00"+b"\x00"*100)
        self.assertIn("SELF",r["format"])
        self.assertIn("warning",r)
        self.assertEqual(r["usb_vidpid_byte_sequence_counts"]["PS4_GHL_USB_BE"],0)
    def test_elf_and_visible_strings(self):
        r=module.inspect_blob("decrypted.elf",b"\x7fELF\x02\x02"+b"\x00"*20+b"cellUsbdOpenPipe\x00"+bytes.fromhex("12ba074b"))
        self.assertEqual(r["elf_endianness"],"big")
        self.assertEqual(r["usb_vidpid_byte_sequence_counts"]["PS3_GHL_USB_BE"],1)
        self.assertTrue(any("cellUsbd" in x for x in r["interesting_visible_strings"]))
    def test_scan_zip_safe(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"game.zip"
            with zipfile.ZipFile(p,"w") as z:
                z.writestr("PS3_GAME/USRDIR/EBOOT.BIN",b"SCE\x00"+b"\x00"*40)
                z.writestr("README.md","Test")
            r=module.read_zip(p)
            self.assertEqual(len(r),1)
            self.assertIn("SELF",r[0]["format"])

if __name__=="__main__":
    unittest.main()
