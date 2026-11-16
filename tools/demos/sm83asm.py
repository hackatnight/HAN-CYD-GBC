"""Game Boy (SM83) assembler for the H@N demo ROM. See asmcore.py for the syntax.

Operands: registers a b c d e h l, af bc de hl sp, [hl] [bc] [de] [hl+] [hl-] [c],
[expr] for memory, conditions nz z nc c. "ldh a,[expr]" / "ldh [expr],a" take the
full $FFxx address. Numbers are expressions.
"""
import re
from asmcore import Assembler, AsmError

R8 = {'b': 0, 'c': 1, 'd': 2, 'e': 3, 'h': 4, 'l': 5, '[hl]': 6, 'a': 7}
R16 = {'bc': 0, 'de': 1, 'hl': 2, 'sp': 3}
R16S = {'bc': 0, 'de': 1, 'hl': 2, 'af': 3}
CC = {'nz': 0, 'z': 1, 'nc': 2, 'c': 3}
ALU = {'add': 0, 'adc': 1, 'sub': 2, 'sbc': 3, 'and': 4, 'xor': 5, 'or': 6, 'cp': 7}
CB = {'rlc': 0, 'rrc': 1, 'rl': 2, 'rr': 3, 'sla': 4, 'sra': 5, 'swap': 6, 'srl': 7}
SIMPLE = {'nop': [0x00], 'halt': [0x76], 'stop': [0x10, 0x00], 'di': [0xF3], 'ei': [0xFB],
          'rlca': [0x07], 'rla': [0x17], 'rrca': [0x0F], 'rra': [0x1F], 'daa': [0x27],
          'cpl': [0x2F], 'scf': [0x37], 'ccf': [0x3F], 'reti': [0xD9]}


class SM83(Assembler):
    def start_bank(self, n):
        self.cur_bank = n
        self.pc = 0 if n == 0 else 0x4000

    def emit(self, data):
        buf = self.out.setdefault(self.cur_bank, bytearray())
        off = self.pc - (0 if self.cur_bank == 0 else 0x4000)
        if off < 0 or off + len(data) > 0x4000:
            raise AsmError('bank %d overflows (pc $%04X)' % (self.cur_bank, self.pc))
        if len(buf) < off:
            buf.extend(b'\xFF' * (off - len(buf)))
        buf[off:off + len(data)] = bytes(data)
        self.pc += len(data)

    def image(self):
        nb = max(self.out) + 1
        size = 0x8000
        while size < nb * 0x4000:
            size *= 2
        img = bytearray(b'\xFF' * size)
        for b, data in self.out.items():
            img[b * 0x4000:b * 0x4000 + len(data)] = data
        return img

    # ------------------------------------------------------------------
    def n8(self, a):
        return self.expr(a, self.final) & 0xFF

    def n16(self, a):
        v = self.expr(a, self.final) & 0xFFFF
        return [v & 0xFF, v >> 8]

    @staticmethod
    def is_mem(a):
        return a.startswith('[') and a.endswith(']')

    def instruction(self, op, args):
        a = [x.lower() if x.lower() in R8 or x.lower() in R16 or x.lower() in R16S or x.lower() in CC
             or x.lower() in ('[bc]', '[de]', '[hl+]', '[hl-]', '[c]') else x for x in args]
        e = self.emit
        if op in SIMPLE and not a:
            return e(SIMPLE[op])
        if op == 'ld':
            d, s = a
            if d in R8 and s in R8:
                if d == '[hl]' and s == '[hl]':
                    raise AsmError('ld [hl],[hl]')
                return e([0x40 | R8[d] << 3 | R8[s]])
            if d in R8 and not self.is_mem(s) and s not in R16:
                return e([0x06 | R8[d] << 3, self.n8(s)])
            if d == 'a' and s in ('[bc]', '[de]'):
                return e([0x0A if s == '[bc]' else 0x1A])
            if s == 'a' and d in ('[bc]', '[de]'):
                return e([0x02 if d == '[bc]' else 0x12])
            if d == 'a' and s == '[hl+]':
                return e([0x2A])
            if d == 'a' and s == '[hl-]':
                return e([0x3A])
            if d == '[hl+]' and s == 'a':
                return e([0x22])
            if d == '[hl-]' and s == 'a':
                return e([0x32])
            if d == 'a' and s == '[c]':
                return e([0xF2])
            if d == '[c]' and s == 'a':
                return e([0xE2])
            if d == 'a' and self.is_mem(s):
                return e([0xFA] + self.n16(s[1:-1]))
            if s == 'a' and self.is_mem(d):
                return e([0xEA] + self.n16(d[1:-1]))
            if d in R16 and s == 'hl' and d == 'sp':
                return e([0xF9])
            if d in R16:
                return e([0x01 | R16[d] << 4] + self.n16(s))
            if self.is_mem(d) and s == 'sp':
                return e([0x08] + self.n16(d[1:-1]))
            raise AsmError('bad ld')
        if op == 'ldh':
            d, s = a
            if d == 'a':
                return e([0xF0, self.expr(s[1:-1], self.final) & 0xFF])
            return e([0xE0, self.expr(d[1:-1], self.final) & 0xFF])
        if op in ALU:
            if len(a) == 2 and a[0] == 'hl' and op == 'add':
                return e([0x09 | R16[a[1]] << 4])
            if len(a) == 2 and a[0] == 'a':
                a = a[1:]
            s = a[0]
            if s in R8:
                return e([0x80 | ALU[op] << 3 | R8[s]])
            return e([0xC6 | ALU[op] << 3, self.n8(s)])
        if op in ('inc', 'dec'):
            r = a[0]
            if r in R8:
                return e([(0x04 if op == 'inc' else 0x05) | R8[r] << 3])
            return e([(0x03 if op == 'inc' else 0x0B) | R16[r] << 4])
        if op in ('push', 'pop'):
            return e([(0xC5 if op == 'push' else 0xC1) | R16S[a[0]] << 4])
        if op == 'jp':
            if a == ['hl']:
                return e([0xE9])
            if len(a) == 2:
                return e([0xC2 | CC[a[0]] << 3] + self.n16(a[1]))
            return e([0xC3] + self.n16(a[0]))
        if op == 'call':
            if len(a) == 2:
                return e([0xC4 | CC[a[0]] << 3] + self.n16(a[1]))
            return e([0xCD] + self.n16(a[0]))
        if op == 'jr':
            target = a[-1]
            off = self.expr(target, self.final) - (self.pc + 2)
            if self.final and not -128 <= off <= 127:
                raise AsmError('jr out of range (%d)' % off)
            opc = 0x18 if len(a) == 1 else 0x20 | CC[a[0]] << 3
            return e([opc, off & 0xFF])
        if op == 'ret':
            if a:
                return e([0xC0 | CC[a[0]] << 3])
            return e([0xC9])
        if op == 'rst':
            return e([0xC7 | self.expr(a[0], True)])
        if op in CB:
            return e([0xCB, CB[op] << 3 | R8[a[0]]])
        if op in ('bit', 'res', 'set'):
            base = {'bit': 0x40, 'res': 0x80, 'set': 0xC0}[op]
            return e([0xCB, base | self.expr(a[0], True) << 3 | R8[a[1]]])
        raise AsmError('unknown instruction ' + op)


NINTENDO_LOGO = bytes.fromhex(
    'CEED6666CC0D000B03730083000C000D0008111F8889000EDCCC6EE6DDDDD999BBBB67636E0EECCCDDDC999FBBB9333E')


def fix_header(img, title, cgb_flag, mbc_type, ram_size=0):
    """Fill in the cartridge header like rgbfix: logo, title, type, sizes and checksums."""
    img[0x104:0x134] = NINTENDO_LOGO
    t = title.encode('ascii')[:15].ljust(15, b'\0')
    img[0x134:0x143] = t
    img[0x143] = cgb_flag
    img[0x144:0x146] = b'\0\0'
    img[0x146] = 0
    img[0x147] = mbc_type
    sizes = {0x8000: 0, 0x10000: 1, 0x20000: 2, 0x40000: 3, 0x80000: 4, 0x100000: 5}
    img[0x148] = sizes[len(img)]
    img[0x149] = ram_size
    img[0x14A] = 1      # non-Japanese
    img[0x14B] = 0x33
    img[0x14C] = 0
    x = 0
    for i in range(0x134, 0x14D):
        x = (x - img[i] - 1) & 0xFF
    img[0x14D] = x
    g = (sum(img) - img[0x14E] - img[0x14F]) & 0xFFFF
    img[0x14E] = g >> 8
    img[0x14F] = g & 0xFF
    return img
