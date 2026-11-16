"""branchfix.py <file.bin> [--write]: per frame, each rotation bone (descriptor 0x05 / 0x07) takes whichever of its two
equivalent angle sets - (a, b, c) or (a + pi, pi - b, c + pi), each component unwrapped toward the previous frame - is
closest to the previous frame. Same rotations, no frame-to-frame branch flips. Hinges (0x06) and positions (0x0B) kept."""
import math, struct, sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read())
t, b, n = struct.unpack_from("<HHI", d, 0)
desc = struct.unpack_from(f"<{b}I", d, 8)
base = 8 + b * 4
V = [list(struct.unpack_from(f"<{b * 3}f", d, base + f * b * 12)) for f in range(n)]
def wrap(x): return (x + math.pi) % (2 * math.pi) - math.pi
def near(prev, e): return [pv + wrap(x - pv) for pv, x in zip(prev, e)]
def maxstep(V):
    return max((abs(V[f + 1][i] - V[f][i]), f, i // 3) for f in range(n - 1) for i in range(b * 3) if desc[i // 3] in (5, 7))
before = maxstep(V)
for f in range(1, n):
    for k in range(b):
        if desc[k] not in (5, 7):
            continue
        prev, e = V[f - 1][k * 3:k * 3 + 3], V[f][k * 3:k * 3 + 3]
        c1 = near(prev, e)
        c2 = near(prev, [e[0] + math.pi, math.pi - e[1], e[2] + math.pi])
        best = min((c1, c2), key=lambda c: sum(abs(x - y) for x, y in zip(c, prev)))
        V[f][k * 3:k * 3 + 3] = best
after = maxstep(V)
print(f"{p.split('/')[-1]:24s} biggest one-frame step on rotations: {before[0]:.3f} (frame {before[1]}, bone {before[2]}) -> {after[0]:.3f} (frame {after[1]}, bone {after[2]})")
if "--write" in sys.argv:
    for f in range(n):
        struct.pack_into(f"<{b * 3}f", d, base + f * b * 12, *V[f])
    open(p, "wb").write(bytes(d))
