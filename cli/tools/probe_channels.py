"""逐通道响度对比：把源 JSON 与目标 JSON 分别抽成「只含某一通道」的 VGM，
用 libvgm 渲染后比较 RMS，判断音量映射是否按比例透传。

只读我们自己的 JSON（寄存器原值），不做任何解析/推断。
"""
import json
import math
import os
import struct
import subprocess
import wave

SONG_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "out",
                        "Spindizzy_II", "31 Grass Maze")
OUT = os.path.join(SONG_DIR, "probes", "chanprobe")
os.makedirs(OUT, exist_ok=True)


def build_vgm(cmd_byte, clock_off, clock_val, events, samples, path):
    """events: [(atSample, reg, val)]，按时间排序后写成单芯片 VGM。"""
    events = sorted(events, key=lambda e: e[0])
    data = bytearray()
    t = 0
    for at, reg, val in events:
        if at > t:
            data += b"\x61" + struct.pack("<H", min(0xFFFF, at - t))
            t = at
        data += bytes([cmd_byte, reg & 0xFF, val & 0xFF])
    data += b"\x66"
    hdr = bytearray(0x40)
    hdr[0:4] = b"Vgm "
    hdr[8:12] = struct.pack("<I", 0x150)
    hdr[clock_off:clock_off + 4] = struct.pack("<I", clock_val)
    hdr[0x18:0x1C] = struct.pack("<I", samples)
    hdr[4:8] = struct.pack("<I", len(hdr) + len(data) - 4)
    with open(path, "wb") as f:
        f.write(bytes(hdr) + data)


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


def opm_events(doc, ch):
    """源侧：只取该通道的寄存器（0x20..0x3F 通道级 + 0x40..0xFF 算子级 + 0x08 key）。"""
    ev = []
    for o in doc.get("orphans", []):
        for r in o["regs"]:
            ev.append((o["atSample"], r["reg"], r["val"], "orphan"))
    for n in doc["notes"]:
        for s in n["segments"]:
            for r in s["regs"]:
                ev.append((s["atSample"], r["reg"], r["val"], "note"))
    keep = []
    for at, reg, val, src in ev:
        if reg == 0x08:
            if (val & 0x07) == ch:
                v = 0x78 if (val & 0x78) else 0x00
                keep.append((at, 0x08, v | ch))
            continue
        if 0x20 <= reg <= 0x3F:
            if (reg & 0x07) == ch:
                keep.append((at, reg, val))
        elif reg >= 0x40:
            if (reg & 0x07) == ch:
                keep.append((at, reg, val))
    # 全局（LFO/test/时钟等）也带上，否则音高/噪声不对
    for g in doc.get("globals", []):
        for r in g["regs"]:
            if r["reg"] < 0x20:
                keep.append((g["atSample"], r["reg"], r["val"]))
    return keep


def opll_events(doc, ch):
    ev = []
    for g in doc.get("globals", []):
        for r in g["regs"]:
            if r["reg"] < 0x10 and r["reg"] < 0x08:
                ev.append((g["atSample"], r["reg"], r["val"]))
    for n in doc["channels"][ch]["notes"]:
        for s in n["segments"]:
            for r in s["regs"]:
                ev.append((s["atSample"], r["reg"], r["val"]))
    return ev


src = json.load(open(os.path.join(SONG_DIR, "song.json"), encoding="utf-8"))
src_ch = src["channels"]

print("ch | 源 OPM RMS | 目标 OPLL RMS | 差 dB")
tot_s, tot_d = 0.0, 0.0
targets = {}
for ver in ["v12", "v13"]:
    p = os.path.join(SONG_DIR, "versions", ver, "song.ym2413.json")
    if os.path.exists(p):
        targets[ver] = json.load(open(p, encoding="utf-8"))

for ver, dst in targets.items():
    print("\n==", ver, "==")
    tot_s = tot_d = 0.0
    for ch in range(8):
        s_ev = opm_events(src_ch[ch], ch)
        d_ev = opll_events(dst, ch)
        sp = os.path.join(OUT, "%s_ch%d_src.vgm" % (ver, ch))
        dp = os.path.join(OUT, "%s_ch%d_dst.vgm" % (ver, ch))
        build_vgm(0x54, 0x30, 3579545, s_ev, 2406316, sp)
        build_vgm(0x51, 0x10, 3579545, d_ev, 2406316, dp)
        try:
            render(sp, sp[:-4] + ".wav")
            render(dp, dp[:-4] + ".wav")
            rs = rms(sp[:-4] + ".wav")
            rd = rms(dp[:-4] + ".wav")
        except subprocess.CalledProcessError:
            print("  FM%d: render failed" % (ch + 1))
            continue
        tot_s += rs * rs
        tot_d += rd * rd
        db = 20 * math.log10(rd / rs) if rs > 0 and rd > 0 else 0
        print("  FM%d  源 %8.1f   目标 %8.1f   %+6.1f dB" % (ch + 1, rs, rd, db))
    print("  合计 %8.1f        %8.1f   %+6.1f dB"
          % (math.sqrt(tot_s), math.sqrt(tot_d),
             20 * math.log10(math.sqrt(tot_d) / math.sqrt(tot_s))))
