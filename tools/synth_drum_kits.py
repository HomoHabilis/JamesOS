#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Synthesise the two built-in drum kits and encode them for the firmware.

Every sound here is generated from the equations below. No recording of any drum machine, sample
pack or other third-party audio is read, so the output is original to this project and is released
under CC0 1.0 (public domain dedication). The kits are *inspired by* the character of classic
analogue drum machines; they are not copies of any product's sounds.

    python3 tools/synth_drum_kits.py            # writes assets/drums/synth/*.wav and the .inc
    python3 tools/synth_drum_kits.py --check    # re-generates in memory and compares with the tree

Output:
  assets/drums/synth/<kit>/<VOICE>.wav     16-bit mono 44.1 kHz, for listening and review
  assets/drums/generated/drum_sample_bank_data.inc   IMA ADPCM, 256-frame blocks (132-byte stride)
"""
import argparse, hashlib, json, struct, sys, wave
from pathlib import Path
import numpy as np

SR = 44100
ROOT = Path(__file__).resolve().parents[1]
VOICES = ("KCK1", "KCK2", "SNAR", "CLAP", "OPEN", "CLOS", "CYMB", "PERC", "KCK3", "RIM", "PEDL", "TOM")
BLOCK_FRAMES, BLOCK_BYTES, DATA_BYTES = 256, 132, 128

STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
)
INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8)


# ---------------------------------------------------------------- building blocks
def t_axis(seconds):
    return np.arange(int(seconds * SR)) / SR


def noise(n, seed):
    return np.random.RandomState(seed).uniform(-1.0, 1.0, n)


def one_pole_lp(x, hz):
    a = 1.0 - np.exp(-2.0 * np.pi * hz / SR)
    y = np.empty_like(x); s = 0.0
    for i, v in enumerate(x):
        s += a * (v - s); y[i] = s
    return y


def highpass(x, hz):
    return x - one_pole_lp(x, hz)


def bandpass(x, lo, hi):
    return one_pole_lp(highpass(x, lo), hi)


def square(f, t):
    return np.sign(np.sin(2.0 * np.pi * f * t) + 1e-9)


def env_exp(t, tau):
    return np.exp(-t / tau)


def sweep_sine(t, f_start, f_end, tau_pitch, tau_amp, drive=1.0):
    f = f_end + (f_start - f_end) * np.exp(-t / tau_pitch)
    phase = 2.0 * np.pi * np.cumsum(f) / SR
    y = np.sin(phase) * env_exp(t, tau_amp)
    return np.tanh(y * drive) / np.tanh(drive) if drive != 1.0 else y


def fade_out(x, ms=6.0):
    n = min(len(x), int(SR * ms / 1000.0))
    x = x.copy(); x[-n:] *= np.linspace(1.0, 0.0, n)
    return x


def finish(x, peak):
    x = x - np.mean(x)                         # no DC
    x = x * (peak * 32767.0 / max(1e-9, np.max(np.abs(x))))
    return np.clip(np.round(x), -32768, 32767).astype(np.int16)


# ---------------------------------------------------------------- the kits
HAT_FREQS = (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)     # six square oscillators, the classic metallic stack


def metal(t, scale=1.0):
    return sum(square(f * scale, t) for f in HAT_FREQS) / len(HAT_FREQS)


def kit_808():
    v = {}
    t = t_axis(0.46)
    v["KCK1"] = finish(fade_out(sweep_sine(t, 200, 46, 0.030, 0.20) + 0.06 * noise(len(t), 1) * env_exp(t, 0.003)), 0.97)
    t = t_axis(0.30)
    v["KCK2"] = finish(fade_out(sweep_sine(t, 260, 58, 0.018, 0.085, 1.6) + 0.10 * noise(len(t), 2) * env_exp(t, 0.002)), 0.97)
    t = t_axis(0.22)
    v["KCK3"] = finish(fade_out(sweep_sine(t, 330, 62, 0.012, 0.060, 2.4) + 0.18 * noise(len(t), 3) * env_exp(t, 0.0015)), 0.97)
    t = t_axis(0.2)
    body = np.sin(2 * np.pi * 180 * t) * env_exp(t, 0.05) + 0.6 * np.sin(2 * np.pi * 330 * t) * env_exp(t, 0.035)
    snap = highpass(noise(len(t), 4), 1800) * env_exp(t, 0.075)
    v["SNAR"] = finish(fade_out(0.8 * body + 0.9 * snap), 0.90)
    t = t_axis(0.22)
    bursts = np.zeros(len(t))
    for k in range(4):
        s = int(k * 0.0105 * SR); e = s + int(0.009 * SR)
        bursts[s:e] = 1.0
    nz = bandpass(noise(len(t), 5), 700, 3200)
    tail = env_exp(np.maximum(t - 0.042, 0), 0.085) * (t >= 0.042)
    v["CLAP"] = finish(fade_out(nz * (bursts * env_exp(t, 0.012) + 0.55 * tail)), 0.86)
    t = t_axis(0.24)
    m = highpass(metal(t, 1.0) + 0.3 * noise(len(t), 6), 6500)
    v["OPEN"] = finish(fade_out(m * env_exp(t, 0.085)), 0.78)
    t = t_axis(0.07)
    m = highpass(metal(t, 1.0) + 0.3 * noise(len(t), 7), 6500)
    v["CLOS"] = finish(fade_out(m * env_exp(t, 0.014), 3.0), 0.76)
    t = t_axis(0.5)
    m = highpass(metal(t, 1.18) + 0.5 * noise(len(t), 8), 4500)
    v["CYMB"] = finish(fade_out(m * (0.7 * env_exp(t, 0.30) + 0.3 * env_exp(t, 0.05))), 0.78)
    t = t_axis(0.12)
    v["PERC"] = finish(fade_out(np.sin(2 * np.pi * 2450 * t) * env_exp(t, 0.012)
                                + 0.5 * np.sin(2 * np.pi * 1740 * t) * env_exp(t, 0.010), 4.0), 0.80)
    t = t_axis(0.07)
    v["RIM"] = finish(fade_out(np.sin(2 * np.pi * 1700 * t) * env_exp(t, 0.006)
                               + 0.7 * np.sin(2 * np.pi * 480 * t) * env_exp(t, 0.010)
                               + 0.5 * highpass(noise(len(t), 9), 2500) * env_exp(t, 0.004), 3.0), 0.84)
    v["PEDL"] = v["CLOS"]
    t = t_axis(0.28)
    v["TOM"] = finish(fade_out(sweep_sine(t, 205, 122, 0.040, 0.11) + 0.05 * noise(len(t), 10) * env_exp(t, 0.004)), 0.92)
    return v


def kit_909():
    v = {}
    t = t_axis(0.36)
    v["KCK1"] = finish(fade_out(sweep_sine(t, 330, 52, 0.020, 0.115, 1.8) + 0.12 * highpass(noise(len(t), 21), 1200) * env_exp(t, 0.004)), 0.97)
    t = t_axis(0.36)
    v["KCK2"] = finish(fade_out(sweep_sine(t, 190, 48, 0.060, 0.15, 1.3)), 0.97)
    t = t_axis(0.22)
    v["KCK3"] = finish(fade_out(sweep_sine(t, 420, 58, 0.010, 0.070, 3.0) + 0.22 * noise(len(t), 22) * env_exp(t, 0.002)), 0.97)
    t = t_axis(0.22)
    body = np.sin(2 * np.pi * 196 * t) * env_exp(t, 0.06) + 0.7 * np.sin(2 * np.pi * 330 * t) * env_exp(t, 0.045)
    snap = highpass(noise(len(t), 23), 1200) * env_exp(t, 0.11)
    v["SNAR"] = finish(fade_out(0.65 * body + 1.0 * snap), 0.90)
    t = t_axis(0.26)
    bursts = np.zeros(len(t))
    for k in range(3):
        s = int(k * 0.011 * SR); e = s + int(0.008 * SR)
        bursts[s:e] = 1.0
    nz = bandpass(noise(len(t), 24), 900, 4200)
    tail = env_exp(np.maximum(t - 0.033, 0), 0.10) * (t >= 0.033)
    v["CLAP"] = finish(fade_out(nz * (bursts * env_exp(t, 0.011) + 0.65 * tail)), 0.86)
    t = t_axis(0.26)
    m = highpass(0.6 * metal(t, 1.4) + 0.8 * noise(len(t), 25), 7000)
    v["OPEN"] = finish(fade_out(m * env_exp(t, 0.10)), 0.80)
    t = t_axis(0.06)
    m = highpass(0.6 * metal(t, 1.4) + 0.8 * noise(len(t), 26), 7000)
    v["CLOS"] = finish(fade_out(m * env_exp(t, 0.012), 3.0), 0.78)
    t = t_axis(0.55)
    m = highpass(0.5 * metal(t, 1.6) + 0.9 * noise(len(t), 27), 5500)
    v["CYMB"] = finish(fade_out(m * (0.6 * env_exp(t, 0.34) + 0.4 * env_exp(t, 0.06))), 0.80)
    t = t_axis(0.09)
    v["PERC"] = finish(fade_out(np.sin(2 * np.pi * 1180 * t) * env_exp(t, 0.014)
                                + 0.6 * highpass(noise(len(t), 28), 3000) * env_exp(t, 0.006), 3.0), 0.82)
    v["RIM"] = v["PERC"]
    v["PEDL"] = v["CLOS"]
    t = t_axis(0.28)
    v["TOM"] = finish(fade_out(sweep_sine(t, 240, 146, 0.030, 0.095) + 0.06 * noise(len(t), 29) * env_exp(t, 0.003)), 0.92)
    return v


KITS = {"808": kit_808, "909": kit_909}


# ---------------------------------------------------------------- IMA ADPCM, 256-frame blocks
def clipped(v):
    return max(-32768, min(32767, v))


def encode_one(target, predictor, index):
    step = STEP_TABLE[index]
    diff = target - predictor
    code = 0
    if diff < 0:
        code = 8; diff = -diff
    delta = step >> 3
    if diff >= step: code |= 4; diff -= step; delta += step
    step >>= 1
    if diff >= step: code |= 2; diff -= step; delta += step
    step >>= 1
    if diff >= step: code |= 1; delta += step
    predictor = clipped(predictor + (-delta if code & 8 else delta))
    index = max(0, min(88, index + INDEX_TABLE[code & 7]))
    return code, predictor, index


def encode_block(samples):
    predictor = int(samples[0]); index = 0
    best = None
    for start in range(0, 89, 4):                 # choose the starting step index with the least error
        p, i, err = predictor, start, 0
        for s in samples[1:24]:
            c, p, i = encode_one(int(s), p, i); err += abs(int(s) - p)
        if best is None or err < best[0]: best = (err, start)
    index = best[1]; init = index
    packed = bytearray(DATA_BYTES)
    for k, s in enumerate(samples[1:]):
        code, predictor, index = encode_one(int(s), predictor, index)
        if k & 1: packed[k >> 1] |= code << 4
        else: packed[k >> 1] = code
    return struct.pack("<hBB", int(samples[0]), init, 0) + bytes(packed)


def encode(samples):
    out = b"".join(encode_block(samples[s:s + BLOCK_FRAMES]) for s in range(0, len(samples), BLOCK_FRAMES))
    return out


def decode(encoded, frames):
    out = []
    for b in range((frames + BLOCK_FRAMES - 1) // BLOCK_FRAMES):
        base = b * BLOCK_BYTES
        predictor, index, _ = struct.unpack_from("<hBB", encoded, base)
        n = min(BLOCK_FRAMES, frames - b * BLOCK_FRAMES)
        out.append(predictor)
        for k in range(n - 1):
            byte = encoded[base + 4 + (k >> 1)]
            code = (byte >> 4) & 15 if k & 1 else byte & 15
            step = STEP_TABLE[index]
            delta = step >> 3
            if code & 4: delta += step
            if code & 2: delta += step >> 1
            if code & 1: delta += step >> 2
            predictor = clipped(predictor + (-delta if code & 8 else delta))
            index = max(0, min(88, index + INDEX_TABLE[code & 7]))
            out.append(predictor)
    return np.array(out, dtype=np.int16)


def snr_db(a, b):
    a = a.astype(float); e = a - b.astype(float)
    return 10 * np.log10((a @ a + 1e-9) / (e @ e + 1e-9))


# ---------------------------------------------------------------- emit
def emit(kits):
    lines = ["/* Generated by tools/synth_drum_kits.py; do not edit by hand. */",
             "/* Original synthesised sounds, CC0 1.0. Include after drum_sample_bank.h in the DSP player TU. */", ""]
    syms, seen, meta = {}, {}, {}
    for kit, voices in kits.items():
        for name in VOICES:
            pcm = voices[name]; enc = encode(pcm)
            key = hashlib.sha256(enc).hexdigest()
            meta[(kit, name)] = (len(pcm), len(enc), snr_db(pcm, decode(enc, len(pcm))))
            if key in seen:
                syms[(kit, name)] = seen[key]; continue
            sym = "br_drum_ima4_%s_%s" % (kit, name.lower())
            seen[key] = sym; syms[(kit, name)] = sym
            lines.append("static const uint8_t %s[%d] = {" % (sym, len(enc)))
            for s in range(0, len(enc), 16):
                lines.append("    " + ", ".join("0x%02x" % b for b in enc[s:s + 16]) + ",")
            lines += ["};", ""]
    lines.append("const br_drum_sample_desc_t br_drum_sample_bank[2][BR_DRUM_VOICE_COUNT] = {")
    for bank, kit in enumerate(("808", "909")):
        lines += ["    /* %d: %s */" % (bank, kit), "    {"]
        for name in VOICES:
            frames, nbytes, _ = meta[(kit, name)]
            lines.append("        { %s, %du, %du, 44100u, BR_DRUM_SAMPLE_CODEC_IMA4 }, /* %s */" % (syms[(kit, name)], frames, nbytes, name))
        lines.append("    },")
    lines += ["};", ""]
    return "\n".join(lines), meta, seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    kits = {k: f() for k, f in KITS.items()}
    text, meta, seen = emit(kits)
    inc = ROOT / "assets/drums/generated/drum_sample_bank_data.inc"
    sizes = {}
    for line in text.splitlines():
        if line.startswith("static const uint8_t"):
            sizes[line.split()[3].split("[")[0]] = int(line.split("[")[1].split("]")[0])
    print("unique payloads: %d, total %d bytes" % (len(sizes), sum(sizes.values())))
    for (kit, name), (frames, nbytes, snr) in meta.items():
        print("  %s %-5s %6d frames %5.0f ms %6d B  SNR %4.1f dB" % (kit, name, frames, 1000 * frames / SR, nbytes, snr))
    if a.check:
        ok = inc.read_text() == text
        print("tree matches generator:", ok); sys.exit(0 if ok else 1)
    for kit, voices in kits.items():
        d = ROOT / "assets/drums/synth" / kit; d.mkdir(parents=True, exist_ok=True)
        for name, pcm in voices.items():
            with wave.open(str(d / (name + ".wav")), "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
    inc.write_text(text, encoding="ascii")
    print("wrote", inc)


if __name__ == "__main__":
    main()
