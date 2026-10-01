"""Tiny typed HLSL-expression -> GLSL translator for WFC MaterialExpressionShaderCode snippets.

Snippets are single expressions over %A..%J inputs (e.g. 'float3(pow(%A,%C)*%D*%E)'). We parse,
type every node with HLSL rules (scalar broadcast, vector truncation to the smaller size) and emit
GLSL with explicit conversions so the result type is known exactly.
"""
import re

T = {1: 'float', 2: 'vec2', 3: 'vec3', 4: 'vec4'}
TOK = re.compile(r'\s*(?:(\d+\.\d*|\.\d+|\d+)[fFhH]?|([A-Za-z_]\w*)|(%[A-J])|(.))')


def conv(code, t, to):
    if t == to: return code
    if t == 1: return '%s(%s)' % (T[to], code)
    if to == 1: return '(%s).x' % code
    if to < t: return '(%s).%s' % (code, 'xyzw'[:to])
    return '%s(%s%s)' % (T[to], code, ', 0.0' * (to - t))


class P:
    def __init__(self, s, args):
        self.toks = []
        for m in TOK.finditer(s):
            if m.group(1) is not None: self.toks.append(('num', m.group(1)))
            elif m.group(2): self.toks.append(('id', m.group(2)))
            elif m.group(3): self.toks.append(('arg', m.group(3)[1]))
            elif m.group(4) and m.group(4).strip(): self.toks.append(('op', m.group(4)))
        self.i = 0
        self.args = args

    def peek(self, k=0):
        return self.toks[self.i + k] if self.i + k < len(self.toks) else ('eof', '')

    def take(self, v=None):
        t = self.peek()
        if v is not None and t[1] != v:
            raise ValueError('expected %s got %s' % (v, t))
        self.i += 1
        return t

    def expr(self):
        return self.add()

    def binop(self, a, b, op):
        ta, tb = a[1], b[1]
        if ta == tb: t = ta
        elif ta == 1: t = tb
        elif tb == 1: t = ta
        else: t = min(ta, tb)
        return '(%s %s %s)' % (conv(a[0], ta, t) if ta != 1 else a[0], op, conv(b[0], tb, t) if tb != 1 else b[0]), t

    def add(self):
        a = self.mul()
        while self.peek()[1] in ('+', '-'):
            op = self.take()[1]
            a = self.binop(a, self.mul(), op)
        return a

    def mul(self):
        a = self.unary()
        while self.peek()[1] in ('*', '/'):
            op = self.take()[1]
            a = self.binop(a, self.unary(), op)
        return a

    def unary(self):
        if self.peek()[1] == '-':
            self.take(); c, t = self.unary(); return '(-%s)' % c, t
        if self.peek()[1] == '+':
            self.take(); return self.unary()
        return self.postfix()

    def postfix(self):
        c, t = self.primary()
        while self.peek()[1] == '.':
            self.take()
            sw = self.take()[1]
            sw = sw.translate(str.maketrans('rgba', 'xyzw'))
            c, t = '(%s).%s' % (conv(c, t, 4) if t == 1 else c, sw), len(sw)
        return c, t

    def call_args(self):
        self.take('(')
        args = []
        if self.peek()[1] != ')':
            args.append(self.expr())
            while self.peek()[1] == ',':
                self.take(); args.append(self.expr())
        self.take(')')
        return args

    def primary(self):
        k, v = self.peek()
        if k == 'num':
            self.take()
            if '.' not in v: v += '.0'
            if v.startswith('.'): v = '0' + v
            if v.endswith('.'): v += '0'
            return v, 1
        if k == 'arg':
            self.take()
            return '(%s)' % self.args[v][0], self.args[v][1]
        if v == '(':
            self.take(); r = self.expr(); self.take(')'); return '(%s)' % r[0], r[1]
        if k == 'id':
            self.take()
            name = v
            m = re.fullmatch(r'(float|half)([234]?)', name)
            a = self.call_args()
            if m:
                n = int(m.group(2) or 1)
                if len(a) == 1:
                    return conv(a[0][0], a[0][1], n), n
                parts = ', '.join(x[0] for x in a)
                return '%s(%s)' % (T[n], parts), n
            return self.fn(name, a)
        raise ValueError('unexpected token %s' % (self.peek(),))

    def fn(self, name, a):
        same = ('saturate', 'abs', 'normalize', 'sqrt', 'frac', 'sin', 'cos', 'exp', 'exp2', 'log', 'log2',
                'floor', 'ceil', 'rsqrt', 'sign')
        g = {'frac': 'fract', 'rsqrt': 'inversesqrt'}.get(name, name)
        if name == 'saturate':
            return 'clamp(%s, 0.0, 1.0)' % a[0][0], a[0][1]
        if name in same:
            return '%s(%s)' % (g, a[0][0]), a[0][1]
        if name in ('dot',):
            t = min(a[0][1], a[1][1]) if a[0][1] != 1 and a[1][1] != 1 else max(a[0][1], a[1][1])
            return 'dot(%s, %s)' % (conv(a[0][0], a[0][1], t), conv(a[1][0], a[1][1], t)), 1
        if name == 'length':
            return 'length(%s)' % a[0][0], 1
        if name == 'cross':
            return 'cross(%s, %s)' % (conv(a[0][0], a[0][1], 3), conv(a[1][0], a[1][1], 3)), 3
        if name == 'pow':
            t = max(a[0][1], a[1][1])
            return 'pow(%s, %s)' % (conv(a[0][0], a[0][1], t), conv(a[1][0], a[1][1], t)), t
        if name in ('min', 'max', 'step', 'fmod', 'atan2', 'reflect'):
            g = {'fmod': 'mod', 'atan2': 'atan'}.get(name, name)
            t = max(a[0][1], a[1][1])
            return '%s(%s, %s)' % (g, conv(a[0][0], a[0][1], t), conv(a[1][0], a[1][1], t)), t
        if name in ('lerp', 'clamp', 'smoothstep'):
            g = {'lerp': 'mix'}.get(name, name)
            t = max(a[0][1], a[1][1]) if name != 'clamp' else a[0][1]
            if name == 'smoothstep': t = a[2][1]
            x = [conv(a[0][0], a[0][1], t), conv(a[1][0], a[1][1], t),
                 a[2][0] if (a[2][1] == 1 and name == 'lerp') else conv(a[2][0], a[2][1], t)]
            if name == 'smoothstep': x = [a[0][0], a[1][0], a[2][0]]
            return '%s(%s, %s, %s)' % (g, x[0], x[1], x[2]), t
        raise ValueError('unknown HLSL function %s' % name)


def translate(code, args):
    """args: {'A': (glsl_code, type)}; unknown %X -> 0.0. Returns (glsl, type)."""
    for k in re.findall(r'%([A-J])', code):
        args.setdefault(k, ('0.0', 1))
    p = P(code, args)
    r = p.expr()
    if p.peek()[0] != 'eof':
        raise ValueError('trailing tokens in %r' % code)
    return r
