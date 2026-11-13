"""量测两颗芯片的音量台阶：YM2151 载波 TL（0..127）与 YM2413 通道音量（0..15）。

做法：手工造最小 VGM，只发一个音的寄存器序列，用 libvgm 的 vgm2wav 渲染，
取 RMS 换算 dB。两张表放在一起才能判定 TL→0..15 的映射该用什么曲线。
"""
import json
import math
import os
import struct
import shutil
import subprocess
import tempfile
import wave

SONG_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "out",
                        "Spindizzy_II", "31 Grass Maze")
OUT = os.path.join(SONG_DIR, "probes", "volprobe")
os.makedirs(OUT, exist_ok=True)
# 渲染用的 wav 只是中间产物，落到系统临时目录，结束即删（不留在 out/ 下）
WAV_DIR = tempfile.mkdtemp(prefix="vgmstudio-probe-")


def build_vgm(clock_off, clock_val, cmds, samples, path):
    # VGM 1.50+：0x34 处的 data offset 为 0 时，数据从 0x40 开始
    data = bytes(cmds) + b"\x66"
    hdr = bytearray(0x40)
    hdr[0:4] = b"Vgm "
    hdr[8:12] = struct.pack("<I", 0x150)
    hdr[clock_off:clock_off + 4] = struct.pack("<I", clock_val)
    hdr[0x18:0x1C] = struct.pack("<I", samples)
    hdr[4:8] = struct.pack("<I", len(hdr) + len(data) - 4)
    with open(path, "wb") as f:
        f.write(bytes(hdr) + data)


def wait(n):
    return b"\x61" + struct.pack("<H", n)


def opm(a, d):
    return b"\x54" + bytes([a & 0xFF, d & 0xFF])


def opll(a, d):
    return b"\x51" + bytes([a & 0xFF, d & 0xFF])


def render(vgm, wav):
    subprocess.run(
        ["libvgm/build/bin/vgm2wav.exe", "--loops", "0", vgm, wav],
        check=True, capture_output=True,
    )


def rms(path):
    w = wave.open(path, "rb")
    raw = w.readframes(w.getnframes())
    d = struct.unpack("<%dh" % (len(raw) // 2), raw)
    return math.sqrt(sum(x * x for x in d) / len(d))


src = json.load(open(os.path.join(SONG_DIR, "song.json"), encoding="utf-8"))
sc = src["channels"][0]
stream = []
for o in sc.get("orphans", []):
    for r in o["regs"]:
        stream.append((o["atSample"], r["reg"], r["val"]))
for n in sc["notes"]:
    for s in n["segments"]:
        for r in s["regs"]:
            stream.append((s["atSample"], r["reg"], r["val"]))
stream.sort(key=lambda x: x[0])
shadow = {}
start = sc["notes"][3]["startSample"]
for at, reg, val in stream:
    if at <= start:
        shadow[reg] = val

alg = shadow[0x20] & 7
CARRY = {0: 8, 1: 8, 2: 8, 3: 8, 4: 12, 5: 14, 6: 14, 7: 15}
carriers = [o for o in range(4) if (CARRY[alg] >> o) & 1]
print("alg", alg, "carriers", carriers,
      "TLs", [shadow.get(0x60 + o * 8, 0) & 0x7F for o in range(4)])


def opm_note(tl_carrier):
    c = bytearray()
    for op in range(4):
        for base in (0x40, 0x80, 0xA0, 0xC0, 0xE0):
            c += opm(base + op * 8, shadow.get(base + op * 8, 0x1F if base >= 0x80 else 0))
        tl = shadow.get(0x60 + op * 8, 0) & 0x7F
        if op in carriers:
            tl = tl_carrier
        c += opm(0x60 + op * 8, tl)
    c += opm(0x20, shadow.get(0x20, 0xC7))
    c += opm(0x28, shadow.get(0x28, 0x4A))
    c += opm(0x30, shadow.get(0x30, 0x00))
    c += opm(0x08, 0x78)
    c += wait(20000)
    c += opm(0x08, 0x00)
    c += wait(2000)
    return c


def opll_note(vol, prog=5):
    c = bytearray()
    c += opll(0x30, (prog << 4) | (vol & 15))
    c += opll(0x10, 0xAD)
    c += opll(0x20, (4 << 1) | 1 | 0x10)
    c += wait(20000)
    c += opll(0x20, (4 << 1) | 1)
    c += wait(2000)
    return c


samples = 23000
print("== YM2151 (alg %d) carrier TL sweep ==" % alg)
base = None
for tl in range(0, 81, 4):
    p = os.path.join(OUT, "o%03d.vgm" % tl)
    build_vgm(0x30, 3579545, opm_note(tl), samples, p)
    render(p, os.path.join(WAV_DIR, os.path.basename(p)[:-4] + ".wav"))
    r = rms(os.path.join(WAV_DIR, os.path.basename(p)[:-4] + ".wav"))
    if base is None:
        base = r
    print("  TL %3d  rms %10.2f  %8.2f dB" % (tl, r, 20 * math.log10(r / base) if r > 0 else -99))

print("== YM2413 channel volume sweep ==")
base = None
for v in range(0, 16):
    p = os.path.join(OUT, "l%02d.vgm" % v)
    build_vgm(0x10, 3579545, opll_note(v), samples, p)
    render(p, os.path.join(WAV_DIR, os.path.basename(p)[:-4] + ".wav"))
    r = rms(os.path.join(WAV_DIR, os.path.basename(p)[:-4] + ".wav"))
    if base is None:
        base = r
    print("  vol %2d  rms %10.2f  %8.2f dB" % (v, r, 20 * math.log10(r / base) if r > 0 else -99))

shutil.rmtree(WAV_DIR, ignore_errors=True)
