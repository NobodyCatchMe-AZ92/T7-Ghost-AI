"""Read-only external sampler (offline replays only) to locate wall-distance and stage-id fields.

Every 0.25 s while a fight is loaded: both live player objects (raw bytes, zlib) -> <out>/objs.bin.
Once per new fight (character pair or round reset) + 10 s later: the exe .data section -> data_<n>.bin.z
Record format (objs.bin): u32 len | zlib(u64 t_ms | i32 c1 | i32 c2 | obj1 bytes | obj2 bytes)
"""
import ctypes
import struct
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import peek  # noqa: E402

SPAN = 0x3670
DATA, DSIZE = 0x3437000, 0x6C1928


def read(h, addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    peek.k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got))
    return buf.raw[:got.value]


def fight_loaded(h, base):
    """ring = [[[exe+0x35037B8]+0x8]] is non-null only while a fight (or replay) is loaded (same
    test as the mod's ring_base(); it is 0 in the Battle Log)."""
    v = ctypes.c_uint64()
    n = ctypes.c_size_t()
    if not peek.k32.ReadProcessMemory(h, ctypes.c_void_p(base + 0x35037B8), ctypes.byref(v), 8, ctypes.byref(n)) or not v.value:
        return False
    w = ctypes.c_uint64()
    if not peek.k32.ReadProcessMemory(h, ctypes.c_void_p(v.value + 8), ctypes.byref(w), 8, ctypes.byref(n)) or not w.value:
        return False
    r = ctypes.c_uint64()
    return bool(peek.k32.ReadProcessMemory(h, ctypes.c_void_p(w.value), ctypes.byref(r), 8, ctypes.byref(n)) and r.value)


def main(out, hours):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    h, base = peek.find_game()
    objs = out / "objs.bin"
    last_pair, fights, dump_due = None, 0, None
    t0 = time.time()
    with open(objs, "ab") as f:
        while time.time() - t0 < hours * 3600:
            c1 = peek.read_i32(h, base + peek.LIVE_P1 + 0xDC)
            c2 = peek.read_i32(h, base + peek.LIVE_P2 + 0xDC)
            if c1 is None:
                break  # game closed
            # a new fight = fight state appears after the Battle Log tore it down (same characters can
            # repeat in back-to-back replays)
            loaded = fight_loaded(h, base)
            if not loaded:
                last_pair = None
            if loaded and 0 <= c1 < 80 and 0 <= c2 < 80:
                pair = (c1, c2)
                if pair != last_pair:
                    fights += 1
                    last_pair = pair
                    (out / f"data_{fights}_a.bin.z").write_bytes(zlib.compress(read(h, base + DATA, DSIZE & ~3), 6))
                    dump_due = time.time() + 10
                    print(f"{time.strftime('%H:%M:%S')} fight {fights} chars={pair}", flush=True)
                if dump_due and time.time() >= dump_due:
                    (out / f"data_{fights}_b.bin.z").write_bytes(zlib.compress(read(h, base + DATA, DSIZE & ~3), 6))
                    dump_due = None
                blob = struct.pack("<Qii", int(time.time() * 1000), c1, c2) + read(h, base + peek.LIVE_P1, SPAN) + read(h, base + peek.LIVE_P2, SPAN)
                z = zlib.compress(blob, 3)
                f.write(struct.pack("<I", len(z)) + z)
            time.sleep(0.25)


if __name__ == "__main__":
    main(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 3)
