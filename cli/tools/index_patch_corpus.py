"""把 out/Spindizzy_II/33 Oily Way/ins/ 下的穷举语料索引成 JSON。

语料是人工在 Furnace 里逐个试听挑出来的 `.fui`，文件名末尾形如 `-13-4`
表示 wave 宏的「首帧 patch - 次帧 patch」。这里统一解一遍，落成机器可读的
一份，供自动匹配 / 选音色时查询。

用法：python cli/tools/index_patch_corpus.py [语料目录] [输出 json]
"""
import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FUITOOL = os.path.join(ROOT, "cli", "build", "fuitool.exe")
DEFAULT_DIR = os.path.join(ROOT, "out", "Spindizzy_II", "33 Oily Way", "ins")
DEFAULT_OUT = os.path.join(ROOT, "docs", "opll-patch-corpus.json")


def dump(path):
    r = subprocess.run([FUITOOL, "dump", path], capture_output=True, text=True)
    return r.stdout


def parse(out):
    rec = {}
    m = re.search(r"opllPreset=(\d+)", out)
    rec["opllPreset"] = int(m.group(1)) if m else None
    for key, pat in (("fingerprint", r"fingerprint\s+:\s*(\S+)"),
                     ("signature", r"signature\s+:\s*(\S+)")):
        m = re.search(pat, out)
        rec[key] = m.group(1) if m else None
    macros = {}
    cur = None
    for line in out.splitlines():
        m = re.match(r"^    (\w+)\s+seq\s+len=(\d+)\s+speed=(\d+)\s+delay=(\d+)"
                     r"\s+loop=(\d+)\s+rel=(\d+)", line)
        if m:
            cur = m.group(1)
            macros[cur] = {"len": int(m.group(2)), "speed": int(m.group(3)),
                           "delay": int(m.group(4)), "loop": int(m.group(5)),
                           "rel": int(m.group(6))}
            continue
        m2 = re.search(r"values: ([\d\s\-]+?)\s+total", line)
        if m2 and cur:
            macros[cur]["values"] = [int(x) for x in m2.group(1).split()]
    rec["macros"] = macros
    return rec


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    dst = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT

    files = sorted(glob.glob(os.path.join(src, "*.fui")))
    entries = []
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        rec = parse(dump(path))
        rec["name"] = name
        rec["file"] = os.path.basename(path)
        rec["family"] = re.sub(r"[\s-]+\d+-\d+$", "", name).strip()
        w = rec["macros"].get("wave")
        vals = w["values"] if w else []
        rec["waveValues"] = vals
        if len(vals) >= 2:
            rec["first"], rec["second"] = vals[0], vals[1]
            m = re.match(r"^(.*?)[\s-]+(\d+)-(\d+)$", name)
            rec["nameMatchesWave"] = (
                m is not None and (int(m.group(2)), int(m.group(3))) == (vals[0], vals[1]))
        entries.append(rec)

    pairs = {}
    for e in entries:
        if "first" not in e:
            continue
        key = f"{e['first']}-{e['second']}"
        pairs.setdefault(key, []).append({
            "name": e["name"], "file": e["file"], "family": e["family"],
            "opllPreset": e["opllPreset"],
            "vol": (e["macros"].get("vol") or {}).get("values"),
        })

    covered = set()
    for e in entries:
        if "first" in e:
            covered.add((e["first"], e["second"]))
    missing = [(a, b) for a in range(1, 16) for b in range(1, 16)
               if a != b and (a, b) not in covered]

    doc = {
        "source": os.path.relpath(src, ROOT).replace("\\", "/"),
        "count": len(entries),
        "coverage": {
            "coveredOrdered": len(covered),
            "totalOrdered": 210,
            "percent": round(100.0 * len(covered) / 210, 1),
            "missing": [f"{a}-{b}" for a, b in missing],
        },
        "pairs": {k: v for k, v in sorted(pairs.items(),
                                          key=lambda kv: (int(kv[0].split("-")[0]),
                                                          int(kv[0].split("-")[1])))},
        "entries": entries,
    }
    with open(dst, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(doc, fh, ensure_ascii=False, indent=1)
    print(f"{len(entries)} entries, {len(pairs)} pairs, "
          f"{len(covered)}/210 covered -> {dst}")


if __name__ == "__main__":
    main()
