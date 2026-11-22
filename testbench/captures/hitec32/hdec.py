#!/usr/bin/env python3
"""hdec.py FILE.vcd [T0_MS T1_MS]: decode Hitec D-series bytes on CH11 from a sigrok VCD export.
115200 bit/s, inverted 8N1 (rest low, start bit high, a 1 bit low). The level at a time is the high-time
fraction over one PRECHARGE sample period (1.0867 us) centred there, > 0.5 = high: this hides the master's
100 ns precharge pulses during PRECHARGE receive. Prints byte groups with start time (ms) and the gap from
the end of the previous group's last stop bit (ms)."""
import sys, re, bisect, os
path = sys.argv[1]
w0 = float(sys.argv[2]) / 1e3 if len(sys.argv) > 2 else None
w1 = float(sys.argv[3]) / 1e3 if len(sys.argv) > 3 else None
ts = 1e-9; ids = {}; ev = {}
for l in open(path):
    if l.startswith('$timescale'):
        m = re.search(r'(\d+)\s*(ns|us|ps)', l); ts = int(m.group(1)) * {'ns': 1e-9, 'us': 1e-6, 'ps': 1e-12}[m.group(2)]
    elif l.startswith('$var'):
        p = l.split(); ids[p[3]] = p[4]
    elif l.startswith('#'):
        p = l[1:].split(); t = int(p[0]) * ts
        for tok in p[1:]:
            ev.setdefault(ids[tok[1:]], []).append((t, int(tok[0])))
lv = []
for t, v in ev['CH11']:
    if lv and lv[-1][1] == v: continue
    lv.append((t, v))
T = [t for t, _ in lv]; V = [v for _, v in lv]
cum = [0.0]  # high time up to T[k]
for k in range(1, len(T)):
    cum.append(cum[-1] + (T[k] - T[k - 1]) * V[k - 1])
def hightime(t):
    k = bisect.bisect_right(T, t) - 1
    if k < 0: return 0.0
    return cum[k] + (t - T[k]) * V[k]
W = float(os.environ.get('WIN', '1.0867e-6'))
def level(t):
    return 1 if (hightime(t + W / 2) - hightime(t - W / 2)) > W / 2 else 0
BIT = 1 / 115200
# candidate start points: every rising edge; filtered by level() a half-bit later
out = []; tend = 0
for k in range(len(T)):
    t = T[k]
    if V[k] != 1 or t < tend: continue
    if w0 is not None and t < w0: continue
    if w1 is not None and t > w1: break
    if level(t + BIT * 0.5) != 1: continue
    val = 0
    for b in range(8):
        val |= (0 if level(t + BIT * (1.5 + b)) else 1) << b
    stop_ok = level(t + BIT * 9.5) == 0
    out.append((t, val, stop_ok)); tend = t + BIT * 9.6
groups = []
for b in out:
    if groups and b[0] - groups[-1][-1][0] < 15 * BIT: groups[-1].append(b)
    else: groups.append([b])
prev = None
for g in groups:
    t0 = g[0][0]; tl = g[-1][0] + 10 * BIT
    gap = '' if prev is None else 'gap %.3f' % ((t0 - prev) * 1e3)
    bad = '' if all(b[2] for b in g) else ' STOP-ERR'
    print('%11.4f ms  end %11.4f  %-30s %s%s' % (t0 * 1e3, tl * 1e3, ' '.join('%02X' % b[1] for b in g), gap, bad))
    prev = tl
c0 = ev.get('CH0', [])
if c0:
    hs = [t for t, v in c0 if v == 1]
    print('# CH0 first high %.4f ms, last edge %.4f ms, edges %d' % (hs[0] * 1e3 if hs else -1, c0[-1][0] * 1e3, len(c0) - 1))
