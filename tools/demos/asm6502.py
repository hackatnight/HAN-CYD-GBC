"""NES (6502) assembler for the H@N demo ROM. See asmcore.py for the syntax.

Operands: #expr, expr, expr,x, expr,y, (expr,x), (expr),y, (expr) for jmp, a for the
accumulator. Zero page is used when the value is known on the first pass and < $100.
The output is a 32 KB PRG image for $8000-$FFFF.
"""
from asmcore import Assembler, AsmError

OPS = {
    'adc': {'imm': 0x69, 'zp': 0x65, 'zpx': 0x75, 'abs': 0x6D, 'absx': 0x7D, 'absy': 0x79, 'indx': 0x61, 'indy': 0x71},
    'and': {'imm': 0x29, 'zp': 0x25, 'zpx': 0x35, 'abs': 0x2D, 'absx': 0x3D, 'absy': 0x39, 'indx': 0x21, 'indy': 0x31},
    'asl': {'acc': 0x0A, 'zp': 0x06, 'zpx': 0x16, 'abs': 0x0E, 'absx': 0x1E},
    'bit': {'zp': 0x24, 'abs': 0x2C},
    'cmp': {'imm': 0xC9, 'zp': 0xC5, 'zpx': 0xD5, 'abs': 0xCD, 'absx': 0xDD, 'absy': 0xD9, 'indx': 0xC1, 'indy': 0xD1},
    'cpx': {'imm': 0xE0, 'zp': 0xE4, 'abs': 0xEC},
    'cpy': {'imm': 0xC0, 'zp': 0xC4, 'abs': 0xCC},
    'dec': {'zp': 0xC6, 'zpx': 0xD6, 'abs': 0xCE, 'absx': 0xDE},
    'eor': {'imm': 0x49, 'zp': 0x45, 'zpx': 0x55, 'abs': 0x4D, 'absx': 0x5D, 'absy': 0x59, 'indx': 0x41, 'indy': 0x51},
    'inc': {'zp': 0xE6, 'zpx': 0xF6, 'abs': 0xEE, 'absx': 0xFE},
    'jmp': {'abs': 0x4C, 'ind': 0x6C},
    'jsr': {'abs': 0x20},
    'lda': {'imm': 0xA9, 'zp': 0xA5, 'zpx': 0xB5, 'abs': 0xAD, 'absx': 0xBD, 'absy': 0xB9, 'indx': 0xA1, 'indy': 0xB1},
    'ldx': {'imm': 0xA2, 'zp': 0xA6, 'zpy': 0xB6, 'abs': 0xAE, 'absy': 0xBE},
    'ldy': {'imm': 0xA0, 'zp': 0xA4, 'zpx': 0xB4, 'abs': 0xAC, 'absx': 0xBC},
    'lsr': {'acc': 0x4A, 'zp': 0x46, 'zpx': 0x56, 'abs': 0x4E, 'absx': 0x5E},
    'ora': {'imm': 0x09, 'zp': 0x05, 'zpx': 0x15, 'abs': 0x0D, 'absx': 0x1D, 'absy': 0x19, 'indx': 0x01, 'indy': 0x11},
    'rol': {'acc': 0x2A, 'zp': 0x26, 'zpx': 0x36, 'abs': 0x2E, 'absx': 0x3E},
    'ror': {'acc': 0x6A, 'zp': 0x66, 'zpx': 0x76, 'abs': 0x6E, 'absx': 0x7E},
    'sbc': {'imm': 0xE9, 'zp': 0xE5, 'zpx': 0xF5, 'abs': 0xED, 'absx': 0xFD, 'absy': 0xF9, 'indx': 0xE1, 'indy': 0xF1},
    'sta': {'zp': 0x85, 'zpx': 0x95, 'abs': 0x8D, 'absx': 0x9D, 'absy': 0x99, 'indx': 0x81, 'indy': 0x91},
    'stx': {'zp': 0x86, 'zpy': 0x96, 'abs': 0x8E},
    'sty': {'zp': 0x84, 'zpx': 0x94, 'abs': 0x8C},
}
IMPLIED = {'brk': 0x00, 'clc': 0x18, 'cld': 0xD8, 'cli': 0x58, 'clv': 0xB8, 'dex': 0xCA, 'dey': 0x88,
           'inx': 0xE8, 'iny': 0xC8, 'nop': 0xEA, 'pha': 0x48, 'php': 0x08, 'pla': 0x68, 'plp': 0x28,
           'rti': 0x40, 'rts': 0x60, 'sec': 0x38, 'sed': 0xF8, 'sei': 0x78, 'tax': 0xAA, 'tay': 0xA8,
           'tsx': 0xBA, 'txa': 0x8A, 'txs': 0x9A, 'tya': 0x98}
BRANCH = {'bcc': 0x90, 'bcs': 0xB0, 'beq': 0xF0, 'bmi': 0x30, 'bne': 0xD0, 'bpl': 0x10, 'bvc': 0x50, 'bvs': 0x70}


class A6502(Assembler):
    def __init__(self, blobs=None):
        super().__init__(blobs)
        self.zp_choice = {}

    def start_bank(self, n):
        raise AsmError('bank is not used on the NES PRG')

    def emit(self, data):
        if not hasattr(self, 'prg') or self.prg_pass != self.final:
            self.prg = bytearray(b'\xFF' * 0x8000)
            self.prg_pass = self.final
        off = self.pc - 0x8000
        if off < 0 or off + len(data) > 0x8000:
            raise AsmError('outside $8000-$FFFF (pc $%04X)' % self.pc)
        self.prg[off:off + len(data)] = bytes(data)
        self.pc += len(data)

    def image(self):
        return self.prg

    def known(self, text):
        try:
            return self.expr(text, True)
        except AsmError:
            return None

    def instruction(self, op, args):
        if op in IMPLIED:
            return self.emit([IMPLIED[op]])
        if op in BRANCH:
            off = self.expr(args[0], self.final) - (self.pc + 2)
            if self.final and not -128 <= off <= 127:
                raise AsmError('branch out of range (%d)' % off)
            return self.emit([BRANCH[op], off & 0xFF])
        if op not in OPS:
            raise AsmError('unknown instruction ' + op)
        modes = OPS[op]
        a = ','.join(args)
        if a == '' or a.lower() == 'a':
            return self.emit([modes['acc']])
        if a.startswith('#'):
            return self.emit([modes['imm'], self.expr(a[1:], self.final) & 0xFF])
        low = a.lower().replace(' ', '')
        if low.startswith('(') and low.endswith(',x)'):
            return self.emit([modes['indx'], self.expr(a[1:a.lower().rindex(',x')], self.final) & 0xFF])
        if low.startswith('(') and low.endswith('),y'):
            return self.emit([modes['indy'], self.expr(a[1:a.rindex(')')], self.final) & 0xFF])
        if low.startswith('(') and low.endswith(')') and op == 'jmp':
            v = self.expr(a[1:-1], self.final)
            return self.emit([modes['ind'], v & 0xFF, v >> 8 & 0xFF])
        idx = ''
        body = a
        if low.endswith(',x') or low.endswith(',y'):
            idx = low[-1]
            body = a[:a.rindex(',')]
        # zero page only when the value is known on the first pass (decision kept for pass 2)
        if not self.final:
            k = self.known(body)
            self.zp_choice[self.lineno] = k is not None and 0 <= k < 0x100
        use_zp = self.zp_choice.get(self.lineno, False)
        v = self.expr(body, self.final)
        zmode = 'zp' + idx
        amode = 'abs' + idx
        if use_zp and zmode in modes:
            return self.emit([modes[zmode], v & 0xFF])
        if amode not in modes:
            raise AsmError('%s has no %s mode' % (op, amode))
        return self.emit([modes[amode], v & 0xFF, v >> 8 & 0xFF])
