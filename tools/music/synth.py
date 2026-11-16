# Tiny chiptune synth: the same model the firmware would use (2 square voices, triangle,
# noise), 22,050 Hz, output squeezed to 8 bits like the board's DAC.
import math, random, wave, struct
SR = 22050
NOTE = {n: i for i, n in enumerate(['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'])}
def freq(tok):
    name, octv = tok[:-1], int(tok[-1])
    return 440.0 * 2 ** ((NOTE[name] + 12 * (octv + 1) - 69) / 12)

def parse(bars):
    """bars: list of strings, 16 steps each, tokens separated by spaces.
    'A4' = new note, '-' = hold, '.' = rest, 'x' = drum hit (noise), 'k' = kick, 'h' = hat."""
    steps = []
    for b in bars:
        t = b.split()
        assert len(t) == 16, (len(t), b)
        steps += t
    return steps

class Voice:
    def __init__(s, kind, vol, duty=0.5, decay=0.0, sustain=1.0, attack=0.0):
        s.kind, s.vol, s.duty, s.decay, s.sustain, s.attack = kind, vol, duty, decay, sustain, attack
def render(song, steps_per_beat=4):
    bpm = song['bpm']; step_len = int(SR * 60 / bpm / steps_per_beat)
    chans = song['chans']
    n_steps = max(len(parse(c['bars'])) for c in chans)
    out = [0.0] * (n_steps * step_len)
    for c in chans:
        v = c['voice']; seq = parse(c['bars']); delay = c.get('delay', 0)
        phase = 0.0; f = 0.0; age = 0; on = False; lfsr = 1; nval = 1.0; drum = None
        for si in range(n_steps):
            tok = seq[(si - delay) % len(seq)]
            if tok == '.': on = False
            elif tok == '-': pass
            elif tok in ('x', 'k', 'h'): on = True; age = 0; drum = tok
            else: on = True; age = 0; f = freq(tok); drum = None
            for k in range(step_len):
                i = si * step_len + k
                if not on: continue
                t = age / SR; age += 1
                env = max(v.sustain, 1.0 - v.decay * t) if v.decay else 1.0
                if v.attack and t < v.attack: env *= t / v.attack
                if v.kind == 'saw':
                    phase = (phase + f / SR) % 1.0
                    s = round((2 * phase - 1) * 7.5) / 7.5   # 16-step saw, soft like the triangle
                elif v.kind == 'sq':
                    phase = (phase + f / SR) % 1.0
                    s = 1.0 if phase < v.duty else -1.0
                elif v.kind == 'tri':
                    phase = (phase + f / SR) % 1.0
                    s = 4 * abs(phase - 0.5) - 1   # 16-step triangle like the NES
                    s = round(s * 7.5) / 7.5
                else:  # noise drums
                    if drum == 'k':   # kick: falling square pitch
                        kf = 150 * math.exp(-t * 25) + 45
                        phase = (phase + kf / SR) % 1.0
                        s = 1.0 if phase < 0.5 else -1.0
                        env = max(0, 1 - t * 9)
                    else:
                        rate = 9000 if drum == 'h' else 3000
                        if int(age * rate / SR) != int((age - 1) * rate / SR):
                            bit = (lfsr ^ (lfsr >> 1)) & 1; lfsr = (lfsr >> 1) | (bit << 14); nval = 1.0 if lfsr & 1 else -1.0
                        s = nval
                        env = max(0, 1 - t * (28 if drum == 'h' else 9))
                out[i] += s * env * v.vol
    return out

def save(name, out, loops=2, tail=0.0):
    pcm = out * loops
    peak = max(1e-9, max(abs(x) for x in pcm))
    g = 0.95 / peak
    with wave.open(name, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(SR)
        w.writeframes(bytes(int(128 + 127 * max(-1, min(1, x * g))) for x in pcm))   # 8-bit like the DAC
    return len(pcm) / SR
