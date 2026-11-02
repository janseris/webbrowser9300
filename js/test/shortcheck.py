# Compares shorttest's digits with Python's repr (shortest round-trip, closest)
import sys, struct
from decimal import Decimal
bad = n = 0
for line in sys.stdin:
    h, dg, K = line.split(); K = int(K)
    v = struct.unpack('>d', bytes.fromhex(h))[0]
    t = Decimal(repr(v)).as_tuple()
    ds = ''.join(map(str, t.digits)).rstrip('0'); ex = t.exponent + (len(t.digits) - len(''.join(map(str, t.digits)).rstrip('0')))
    n += 1
    if ds != dg or ex != K:
        bad += 1
        if bad < 20: print('diff', repr(v), 'repr', ds, ex, 'ours', dg, K)
print(n, 'doubles,', bad, 'differences')
