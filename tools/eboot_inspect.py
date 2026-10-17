#!/usr/bin/env python3
"""Offline PS3 EBOOT/SFO triage, designed to keep game binaries out of public repos.

Run: python3 tools/eboot_inspect.py /path/to/files.zip -o report.json

No external dependencies. Understands ZIP and unzipped EBOOT/SELF/ELF/PARAM.SFO.
It DOES NOT decrypt encrypted PS3 SELF/NPDRM files and cannot substitute for a
decrypted PPU ELF or real PS3 title imports.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
import zipfile
from pathlib import Path

KEYWORDS = ("cellusbd", "sys_usbd", "usb", "hid", "guitar", "dongle",
            "12ba", "074b", "1430", "07bb", "nids", "module")
TARGETS = {
    "PS3_GHL_USB_BE": bytes.fromhex("12ba074b"),
    "PS3_GHL_USB_LE": bytes.fromhex("ba124b07"),
    "PS4_GHL_USB_BE": bytes.fromhex("143007bb"),
    "PS4_GHL_USB_LE": bytes.fromhex("3014bb07"),
}
MAX_FILE = 120 * 1024 * 1024

def sfo_info(data):
    if len(data) < 20 or data[:4] != b"\x00PSF": return {}
    try:
        _, version, keys_start, data_start, count = struct.unpack_from("<5I", data, 0)
        result = {"_sfo_version": hex(version)}
        for i in range(min(count, 256)):
            off = 20 + 16 * i
            if off + 16 > len(data): break
            k_off, fmt, _, length, maxlen, d_off = struct.unpack_from("<HBBIII",data,off)
            ks = keys_start+k_off
            ds = data_start+d_off
            if ks >= len(data) or ds >= len(data):continue
            end = data.find(b"\0",ks)
            if end < 0:continue
            key=data[ks:end].decode("ascii","replace")
            if key not in ("TITLE_ID","TITLE","APP_VER","VERSION",
                           "CATEGORY","PS3_SYSTEM_VER"):continue
            raw=data[ds:ds+min(length,maxlen,4096)]
            if fmt == 4 and len(raw)>=4: value=struct.unpack_from("<I",raw)[0]
            else:value=raw.rstrip(b"\0").decode("utf-8","replace")
            result[key]=value
        return result
    except (ValueError, struct.error):
        return {"error":"Malformed PARAM.SFO"}

def printable_strings(data):
    chunks=re.findall(rb"[\x20-\x7e]{5,}",data)
    filtered=[]
    for s in chunks:
        low=s.lower()
        if any(w.encode() in low for w in KEYWORDS):
            try:
                v=s.decode("ascii")
                if len(v)>210:v=v[:207]+"..."
                if v not in filtered:filtered.append(v)
            except UnicodeDecodeError:pass
        if len(filtered)>=100:break
    return filtered

def inspect_blob(name, raw):
    kind = "other/unknown"
    if raw.startswith(b"\x7fELF"):kind="ELF (already decrypted or ELF container)"
    elif raw.startswith(b"SCE\x00"):kind="SCE SELF container (may be encrypted)"
    elif raw.startswith(b"\x00PSF"):kind="PARAM.SFO"
    result = {
        "name":name, "size_bytes":len(raw),
        "sha256":hashlib.sha256(raw).hexdigest(),
        "format":kind,
        "header_hex":raw[:64].hex(" "),
        "sfo":sfo_info(raw) if kind=="PARAM.SFO" else {},
    }
    if kind.startswith("ELF"):
        result["elf_class"]="ELF64" if len(raw)>5 and raw[4]==2 else "ELF32"
        result["elf_endianness"]="big" if len(raw)>6 and raw[5]==2 else "little"
    elif "SELF" in kind:
        result["warning"]="SCE SELF header confirmed; the protected executable code may not be readable until decrypted."
    if "EBOOT" in name.upper() or kind.startswith("ELF"):
        result["embedded_elf_offsets"]=[
            i for i in range(len(raw)) if raw.startswith(b"\x7fELF",i)
        ][:20]
        result["usb_vidpid_byte_sequence_counts"]={
            k:raw.count(v) for k,v in TARGETS.items()
        }
        result["interesting_visible_strings"]=printable_strings(raw)
    return result

def read_zip(p):
    items=[]
    with zipfile.ZipFile(p) as z:
        for f in z.infolist():
            if f.is_dir():continue
            basename=Path(f.filename.replace("\\","/")).name.upper()
            if basename not in ("EBOOT.BIN","PARAM.SFO") and not basename.endswith(".SELF"):
                continue
            if f.file_size>MAX_FILE:
                items.append({"name":f.filename,"error":"Entry exceeds safety size limit"})
                continue
            try:
                raw=z.read(f)
                items.append(inspect_blob(f.filename,raw))
            except (RuntimeError,zipfile.BadZipFile,MemoryError) as e:
                items.append({"name":f.filename,"error":str(e)})
    return items

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("file",type=Path)
    ap.add_argument("-o","--output",type=Path,default=Path("eboot_import_triage.json"))
    args=ap.parse_args()
    try:
        if zipfile.is_zipfile(args.file):
            items=read_zip(args.file)
        elif args.file.stat().st_size<=MAX_FILE:
            items=[inspect_blob(args.file.name,args.file.read_bytes())]
        else:
            raise ValueError("Input too large")
    except (ValueError, OSError, zipfile.BadZipFile) as e:
        print(f"Unable to read file: {e}",file=sys.stderr)
        return 1
    report={"source_name":args.file.name,"matched_files":items,
            "observations":[
                "The presence of string/VID-PID bytes does not prove those values are used as USB identity checks.",
                "No string hits in encrypted SELF does not prove USB code or imports are absent.",
                "ELF import analysis requires decrypted executable sections and ideally PPU module/NID parsing.",
                "No files from this analysis are sent to the network by this script."
            ]}
    args.output.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    print("Input files:",len(items))
    for item in items:print(item["name"],item.get("format",item.get("error","unknown")),
                            "size=",item.get("size_bytes","?"))
    print("Written",args.output)
    return 0

if __name__=="__main__":sys.exit(main())
