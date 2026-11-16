"""The demo ROMs' chiptunes as step data: the two menu tunes from the firmware (Night Drive,
Starfield), Pixel Parade (written with them), a new one for the demos (Vector Balls), and
the original demo arpeggio. Step codes match tools/music/gen_tunes.py: 0 = hold, 1 = rest,
2 = kick, 3 = hat, 4 = snare, 12+ = MIDI note.  Run it to write WAV previews:
    python3 tunes.py OUTDIR
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'music'))
from synth import Voice, parse, render, save, NOTE          # noqa: E402
import compose                                               # noqa: E402
from compose import CH, chk, arp, bass, drums                # noqa: E402
from starfield2 import a as starfield                       # noqa: E402

# ---- Vector Balls: D minor, 124 bpm, new for the demos
CH['Dm'] = ['D4', 'F4', 'A4', 'D5', 'F5', 'A5']
CH['Bb'] = ['A#3', 'D4', 'F4', 'A#4', 'D5', 'F5']
CH['A'] = ['A3', 'C#4', 'E4', 'A4', 'C#5', 'E5']
compose.ROOT.update({'Dm': 'D', 'Bb': 'A#', 'A': 'A'})
prog4 = ['Dm', 'Bb', 'C', 'A', 'Dm', 'Bb', 'C', 'A']
lead4 = chk('lead4', [
    'D5 - - - A5 - - - F5 - E5 - D5 - - -', 'F5 - - - - - D5 - A#4 - - - . . . .',
    'E5 - G5 - C6 - - - G5 - E5 - C5 - - -', 'C#5 - - - E5 - - - A5 - - - G5 - E5 -',
    'F5 - A5 - D6 - - - C6 - A5 - F5 - - -', 'D6 - - - A#5 - - - F5 - D5 - . . . .',
    'E5 - - - G5 - C6 - E6 - - - D6 - C6 -', 'A5 - - - - - - - E5 - C#5 - A4 - - -'])


def bass4(c):
    r = compose.ROOT[c]
    return ' '.join([r + '2', '.', r + '3', r + '2'] * 4)


vector_balls = {'bpm': 124, 'chans': [
    {'voice': Voice('sq', 0.26, 0.5, decay=1.0, sustain=0.5), 'bars': lead4},
    {'voice': Voice('sq', 0.12, 0.125, decay=5.0, sustain=0.2), 'bars': chk('arp4', [arp(c) for c in prog4])},
    {'voice': Voice('tri', 0.42), 'bars': chk('bass4', [bass4(c) for c in prog4])},
    {'voice': Voice('noise', 0.2), 'bars': chk('dr4', ['k . h h x . h . k . h k x . h h'] * 8)}]}

# ---- the original demo arpeggio (Am F C G), 16th notes at 7 frames a step
ARP_CHORDS = [('A4', 'C5', 'E5', 'A5'), ('F4', 'A4', 'C5', 'F5'), ('C5', 'E5', 'G5', 'C6'), ('G4', 'B4', 'D5', 'G5')]
ARP_BASS = ['A2', 'F2', 'C3', 'G2']
ARP_ORDER = [0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 2, 1, 3]
arpeggio = {'bpm': 3600 / 7 / 4, 'chans': [
    {'voice': Voice('sq', 0.25, 0.5, decay=3.0, sustain=0.2),
     'bars': [' '.join(ARP_CHORDS[b][ARP_ORDER[s]] for s in range(16)) for b in range(4)]},
    {'voice': Voice('sq', 0.3, 0.25, decay=1.0, sustain=0.4),
     'bars': [' '.join((ARP_BASS[b][:-1] + str(int(ARP_BASS[b][-1]) + (1 if s % 8 == 4 else 0))) if s % 4 == 0 else '-'
                       for s in range(16)) for b in range(4)]}]}

TUNES = [('NIGHT DRIVE', compose.song1), ('STARFIELD', starfield), ('PIXEL PARADE', compose.song2),
         ('VECTOR BALLS', vector_balls), ('ARPEGGIO', arpeggio)]


def code(tok):
    if tok == '-':
        return 0
    if tok == '.':
        return 1
    if tok in ('k', 'h', 'x'):
        return {'k': 2, 'h': 3, 'x': 4}[tok]
    return NOTE[tok[:-1]] + 12 * (int(tok[-1]) + 1)


def tune_data(song):
    """-> dict: frames per step (8.8), steps, and per hardware channel (p1, p2, wave, noise):
    (codes, voice) or None. Square voices go to the pulses (the louder one to pulse 1)."""
    n = max(len(parse(c['bars'])) for c in song['chans'])
    out = {'speed': round(3600 / song['bpm'] / 4 * 256), 'steps': n, 'p1': None, 'p2': None, 'wave': None, 'noise': None}
    sq = []
    for c in song['chans']:
        seq = parse(c['bars'])
        d = c.get('delay', 0)
        codes = [code(seq[(i - d) % len(seq)]) for i in range(n)]
        k = c['voice'].kind
        if k == 'sq':
            sq.append((codes, c['voice']))
        elif k == 'tri':
            out['wave'] = (codes, c['voice'])
        elif k == 'noise':
            out['noise'] = (codes, c['voice'])
    sq.sort(key=lambda cv: -cv[1].vol)
    for slot, cv in zip(('p1', 'p2'), sq):
        out[slot] = cv
    return out


if __name__ == '__main__':
    od = sys.argv[1] if len(sys.argv) > 1 else '.'
    for name, song in TUNES:
        fn = os.path.join(od, name.lower().replace(' ', '-') + '.wav')
        print(name, round(save(fn, render(song), 1), 1), 's')
