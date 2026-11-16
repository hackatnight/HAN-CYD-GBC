# The menu tunes (and the one that wasn't picked), written for H@N-CYD-GBC. Run it to render WAV previews.
from synth import *
CH = {'Am':['A3','C4','E4','A4','C5','E5'], 'F':['F3','A3','C4','F4','A4','C5'], 'C':['C4','E4','G4','C5','E5','G5'],
      'G':['G3','B3','D4','G4','B4','D5'], 'E':['E3','G#3','B3','E4','G#4','B4'], 'Em':['E4','G4','B4','E5','G5','B5'],
      'Cmaj7':['C4','E4','G4','B4','E5','G5'], 'Am7':['A3','C4','E4','G4','C5','E5'], 'B':['B3','D#4','F#4','B4','D#5','F#5']}
ROOT = {'Am':'A','F':'F','C':'C','G':'G','E':'E','Em':'E','Cmaj7':'C','Am7':'A','B':'B'}
def arp(chord, pat=(0,2,4,5,4,2,3,1)):
    t = CH[chord]; return ' '.join(t[pat[i % len(pat)]] for i in range(16))
def bass(chord, style):
    r = ROOT[chord]
    if style == 'drive': seq = [r+'2','-',r+'3','-',r+'2','-',r+'3','-',r+'2','-',r+'3','-',r+'2','-',r+'3','-']
    elif style == 'bounce':
        fifth = {'C':'G','A':'E','F':'C','G':'D','E':'B'}[r]
        seq = [r+'2','.',fifth+'2','.',r+'3','.',fifth+'2','.',r+'2','.',fifth+'2','.',r+'3','.',fifth+'2','.']
    return ' '.join(seq)
def drums(style):
    if style == 'drive': return 'k . h . x . h . k . h k x . h .'
    if style == 'bounce': return 'k . h . x . h . k k h . x . h h'
    return 'h . . . h . . . h . . . h . . .'
def chk(name, bars):
    for i, b in enumerate(bars): assert len(b.split()) == 16, (name, i + 1, len(b.split()), b)
    return bars

# 1. "Night Drive": A minor, 112 bpm, driving
prog1 = ['Am','F','C','G','Am','F','G','E'] * 2
lead1 = chk('lead1', [
 'E5 - - - D5 - C5 - E5 - - - A4 - - -', 'F5 - - - E5 - C5 - A4 - - - . . . .',
 'G5 - - - E5 - D5 - C5 - - - E5 - G5 -', 'D5 - - - - - B4 - D5 - - - . . . .',
 'E5 - A5 - G5 - E5 - C5 - - - D5 - E5 -', 'F5 - - - A5 - G5 - F5 - - - E5 - C5 -',
 'D5 - - - G5 - - - B4 - D5 - G5 - - -', 'G#5 - - - - - - - E5 - - - . . . .',
 'A5 - - - G5 - E5 - G5 - A5 - C6 - - -', 'A5 - - - - - G5 - F5 - - - C5 - - -',
 'E5 - G5 - C6 - - - B5 - G5 - E5 - - -', 'D5 - - - G5 - - - B5 - - - . . D6 -',
 'C6 - - - B5 - A5 - E5 - - - A5 - - -', 'F5 - A5 - C6 - A5 - F5 - - - E5 - D5 -',
 'B4 - D5 - G5 - - - F5 - E5 - D5 - - -', 'E5 - - - - - - - G#4 - B4 - E5 - - -'])
song1 = {'bpm': 112, 'chans': [
  {'voice': Voice('sq', 0.30, 0.25, decay=0.6, sustain=0.55), 'bars': lead1},
  {'voice': Voice('sq', 0.13, 0.125, decay=4.0, sustain=0.25), 'bars': chk('arp1', [arp(c) for c in prog1])},
  {'voice': Voice('tri', 0.42), 'bars': chk('bass1', [bass(c, 'drive') for c in prog1])},
  {'voice': Voice('noise', 0.22), 'bars': chk('dr1', [drums('drive')] * 16)}]}

# 2. "Pixel Parade": C major, 144 bpm, bouncy
prog2 = ['C','Am','F','G','C','Am','F','G','F','G','Em','Am','F','G','C','C']
CH['Em3'] = ['E3','G3','B3','E4','G4','B4']
lead2 = chk('lead2', [
 'C5 . E5 . G5 - E5 . C6 - - - G5 - . .', 'A5 . G5 . E5 - C5 . E5 - - - . . . .',
 'F5 . A5 . C6 - A5 . F5 - G5 - A5 - . .', 'G5 - - - B5 - - - D6 - - - . . . .',
 'E5 . G5 . C6 - B5 . C6 - - - E6 - . .', 'D6 . C6 . A5 - G5 . A5 - - - . . . .',
 'A5 . G5 . F5 - E5 . F5 - A5 - C6 - . .', 'B5 - - - G5 - - - D5 - E5 - F5 - G5 -',
 'A5 - - - C6 - - - A5 - G5 - F5 - - -', 'G5 - - - B5 - - - D6 - - - B5 - - -',
 'E5 - G5 - B5 - E6 - D6 - B5 - G5 - - -', 'A5 - - - - - - - C6 - B5 - A5 - G5 -',
 'F5 - A5 - C6 - F6 - E6 - C6 - A5 - - -', 'G5 - B5 - D6 - G6 - F6 - D6 - B5 - - -',
 'C6 - - - G5 - E5 - C5 - E5 - G5 - - -', 'C6 - - - - - - - . . . . . . . .'])
def stabs(chord):
    t = CH['Em3' if chord == 'Em' else chord]
    return ' '.join(t[3 + (i // 2) % 3] if i % 4 == 2 else '.' for i in range(16))
song2 = {'bpm': 144, 'chans': [
  {'voice': Voice('sq', 0.30, 0.5, decay=1.2, sustain=0.5), 'bars': lead2},
  {'voice': Voice('sq', 0.14, 0.25, decay=9.0, sustain=0.0), 'bars': chk('stab2', [stabs(c) for c in prog2])},
  {'voice': Voice('tri', 0.45), 'bars': chk('bass2', [bass('Am' if c == 'Em' and False else c, 'bounce') if c != 'Em' else bass('E', 'bounce') for c in prog2])},
  {'voice': Voice('noise', 0.2), 'bars': chk('dr2', [drums('bounce')] * 16)}]}

# 3. "Starfield": E minor, 92 bpm, arpeggio with an echo, slow triangle melody
prog3 = ['Em','Cmaj7','Am7','B','Em','Cmaj7','Am7','B']
mel3 = chk('mel3', [
 'E5 - - - - - - - B4 - - - - - - -', 'G5 - - - - - - - E5 - - - D5 - - -',
 'C5 - - - - - - - E5 - - - A4 - - -', 'B4 - - - - - - - D#5 - - - F#5 - - -',
 'G5 - - - - - - - F#5 - - - E5 - - -', 'E5 - - - - - - - G5 - - - B5 - - -',
 'A5 - - - - - - - G5 - - - E5 - - -', 'F#5 - - - - - - - D#5 - - - B4 - - -'])
arp3 = chk('arp3', [arp(c, (0,1,2,3,4,5,4,3)) for c in prog3])
song3 = {'bpm': 92, 'chans': [
  {'voice': Voice('sq', 0.20, 0.125, decay=5.0, sustain=0.2), 'bars': arp3},
  {'voice': Voice('sq', 0.09, 0.125, decay=5.0, sustain=0.2), 'bars': arp3, 'delay': 3},   # echo
  {'voice': Voice('tri', 0.50), 'bars': mel3},
  {'voice': Voice('noise', 0.07), 'bars': chk('dr3', [drums('soft')] * 8)}]}

if __name__ == '__main__':
  for name, song, loops in (('1-night-drive', song1, 1), ('2-pixel-parade', song2, 1), ('3-starfield', song3, 2)):
    secs = save(name + '.wav', render(song), loops)
    print(f'{name}: {secs:.1f} s')
