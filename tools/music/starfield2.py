# Starfield, version A (picked): arpeggio + echo + soft hats, the triangle plays a quiet bass.
from compose import CH, ROOT, arp, chk, prog3, mel3, arp3, drums
from synth import *
# soft bass: the chord's root, long notes, low octave
def softbass(c):
    r = {'Em':'E2','Cmaj7':'C3','Am7':'A2','B':'B2'}[c]
    return f'{r} - - - - - - - {r} - - - - - - -'
bass3 = chk('bass3', [softbass(c) for c in prog3])
base = [
  {'voice': Voice('sq', 0.20, 0.125, decay=5.0, sustain=0.2), 'bars': arp3},
  {'voice': Voice('sq', 0.09, 0.125, decay=5.0, sustain=0.2), 'bars': arp3, 'delay': 3},
  {'voice': Voice('noise', 0.07), 'bars': chk('dr3', [drums('soft')] * 8)}]
# A: no melody, the triangle plays a quiet bass instead
a = {'bpm': 92, 'chans': base + [{'voice': Voice('tri', 0.22), 'bars': bass3}]}
# B: the melody on a quiet saw with a soft start, one octave lower
low = [' '.join(t[:-1] + str(int(t[-1]) - 1) if t[-1].isdigit() else t for t in b.split()) for b in mel3]
b = {'bpm': 92, 'chans': base + [{'voice': Voice('saw', 0.10, attack=0.12, decay=0.25, sustain=0.6), 'bars': low}]}
if __name__ == '__main__':
    print('A', save('3a-starfield-no-melody.wav', render(a), 2))
    print('B', save('3b-starfield-soft-saw.wav', render(b), 2))
