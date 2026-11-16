"""Shared two-pass assembler core for the H@N demo ROMs (Game Boy SM83 and NES 6502).

Source syntax (both CPUs):
  label:              global label          .loop:   local label (belongs to the last global)
  NAME = expr         constant              ; comment
  org expr            set the address       (banked ROMs: bank N, see below)
  bank N              start ROM bank N      (Game Boy: 16 KB banks, N>=1 at $4000)
  db a, "text", ...   bytes                 dw a, b ...        16-bit little-endian words
  ds n [, fill]       n bytes               align n            pad to a multiple of n
  incbin NAME         bytes from the blobs dict given by build.py
Expressions are Python expressions over numbers ($FF, %1010, 123), symbols, and
LOW(x), HIGH(x), BANK(label) (the ROM bank a label lives in).
"""
import re


class AsmError(Exception):
    pass


NUM = re.compile(r'\$([0-9A-Fa-f]+)|%([01]+)')


class Assembler:
    bank_size = 0x4000      # Game Boy default; the 6502 assembler overrides placement

    def __init__(self, blobs=None):
        self.blobs = blobs or {}
        self.symbols = {}
        self.label_bank = {}

    # ------------------------------------------------------------------ helpers
    def expr(self, text, final):
        t = text.strip()
        t = NUM.sub(lambda m: str(int(m.group(1), 16)) if m.group(1) else str(int(m.group(2), 2)), t)
        t = re.sub(r'BANK\((\.?\w+)\)',
                   lambda m: str(self.label_bank.get(self.label_key(m.group(1)), 0)), t)
        t = re.sub(r'(?<![\w.])\.(\w+)', lambda m: '__L_' + self.scope + '__' + m.group(1), t)
        env = dict(self.symbols)
        env.update(LOW=lambda x: x & 0xFF, HIGH=lambda x: (x >> 8) & 0xFF)
        try:
            return int(eval(t, {'__builtins__': {}}, env))
        except NameError:
            if final:
                raise AsmError('unknown symbol in "%s"' % text)
            return 0

    def label_key(self, name):
        if name.startswith('.'):
            return '__L_' + self.scope + '__' + name[1:]
        return name

    # ------------------------------------------------------------------ driver
    def assemble(self, source):
        lines = source.split('\n')
        for final in (False, True):
            self.out = {}          # bank -> bytearray (Game Boy) or a flat image (6502)
            self.scope = ''
            self.cur_bank = 0
            self.pc = 0
            self.final = final
            for no, raw in enumerate(lines, 1):
                line = self.strip_comment(raw)
                self.lineno = no
                try:
                    self.line(line)
                except AsmError as e:
                    raise AsmError('line %d: %s\n  %s' % (no, e, raw.strip()))
                except Exception as e:
                    raise AsmError('line %d: %s: %s\n  %s' % (no, type(e).__name__, e, raw.strip()))
        return self.image()

    @staticmethod
    def strip_comment(raw):
        out, q = [], False
        for ch in raw:
            if ch == '"':
                q = not q
            if ch == ';' and not q:
                break
            out.append(ch)
        return ''.join(out).rstrip()

    def line(self, line):
        if not line.strip():
            return
        m = re.match(r'^\s*([A-Za-z_.][\w.]*):\s*(.*)$', line)
        if m:
            name = m.group(1)
            if not name.startswith('.'):
                self.scope = name
            key = self.label_key(name)
            if not self.final and key in self.symbols and key in self.label_bank:
                raise AsmError('label defined twice: ' + name)
            self.symbols[key] = self.pc
            self.label_bank[key] = self.cur_bank
            line = m.group(2)
            if not line.strip():
                return
        m = re.match(r'^\s*([A-Za-z_]\w*)\s*=\s*(.+)$', line)
        if m:
            self.symbols[m.group(1)] = self.expr(m.group(2), self.final)
            return
        parts = line.strip().split(None, 1)
        op = parts[0].lower()
        args = parts[1] if len(parts) > 1 else ''
        if op == 'org':
            self.pc = self.expr(args, True)
        elif op == 'bank':
            self.start_bank(self.expr(args, True))
        elif op == 'db':
            for a in self.split_args(args):
                if a.startswith('"'):
                    self.emit(a[1:-1].encode('ascii'))
                else:
                    self.emit([self.expr(a, self.final) & 0xFF])
        elif op == 'dw':
            for a in self.split_args(args):
                v = self.expr(a, self.final)
                self.emit([v & 0xFF, (v >> 8) & 0xFF])
        elif op == 'ds':
            a = self.split_args(args)
            n = self.expr(a[0], True)
            fill = self.expr(a[1], True) if len(a) > 1 else 0
            self.emit([fill] * n)
        elif op == 'align':
            n = self.expr(args, True)
            pad = (-self.pc) % n
            self.emit([0] * pad)
        elif op == 'incbin':
            self.emit(self.blobs[args.strip()])
        else:
            self.instruction(op, self.split_args(args))

    @staticmethod
    def split_args(args):
        out, depth, q, cur = [], 0, False, ''
        for ch in args:
            if ch == '"':
                q = not q
            if not q:
                if ch in '([':
                    depth += 1
                elif ch in ')]':
                    depth -= 1
                elif ch == ',' and depth == 0:
                    out.append(cur.strip())
                    cur = ''
                    continue
            cur += ch
        if cur.strip():
            out.append(cur.strip())
        return out

    # subclasses: start_bank, emit, image, instruction
