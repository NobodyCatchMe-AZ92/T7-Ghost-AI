"""Replay pilot: watches t7ghost/status.txt, screenshots the game window, presses menu keys.

Advances through your own saved replays in the Battle Log while the dev build records them.
Stdlib only (ctypes + zlib). Run with `py -I tools/replay_pilot.py <command>`.

  shot OUT.png [--scale S]   capture the game window client area
  keys K1,K2,...             tap keys (scancodes) into the focused game window
  wait-end [--timeout S]     block until the current replay ends (frame stalls or mode leaves 3)
  wait-start [--timeout S]   block until a replay is playing (mode 3 and frame advancing)
"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import time
import zlib
from pathlib import Path

STATUS = Path(r"C:\Program Files (x86)\Steam\steamapps\common\TEKKEN 7\TekkenGame\Binaries\Win64\t7ghost\status.txt")
GAME_EXE = "TekkenGame-Win64-Shipping.exe"

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
# physical pixels everywhere (otherwise a scaled desktop shifts the capture by the title bar)
try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # PER_MONITOR_AWARE_V2
except Exception:
    pass

# DirectInput scancodes for the keys the menus might need
SCAN = {
    "up": 0xC8, "down": 0xD0, "left": 0xCB, "right": 0xCD,  # arrows (extended)
    "enter": 0x1C, "esc": 0x01, "space": 0x39, "backspace": 0x0E,
    "w": 0x11, "a": 0x1E, "s": 0x1F, "d": 0x20, "u": 0x16, "i": 0x17, "j": 0x24, "k": 0x25,
    "f": 0x21, "e": 0x12, "q": 0x10, "r": 0x13, "tab": 0x0F,
}


def read_status():
    try:
        lines = STATUS.read_text().splitlines()
    except OSError:
        return {}
    out = {}
    for line in lines[:2]:
        for tok in line.split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                out[k] = v
    return out


def game_hwnd():
    found = []
    pid_target = None
    # find PID by enumerating windows and checking process image name
    psapi = ctypes.WinDLL("psapi")
    EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        h = kernel32.OpenProcess(0x1000, False, pid.value)  # QUERY_LIMITED_INFORMATION
        if h:
            buf = ctypes.create_unicode_buffer(512)
            size = wt.DWORD(512)
            if kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)) and buf.value.lower().endswith(GAME_EXE.lower()):
                r = wt.RECT()
                user32.GetClientRect(hwnd, ctypes.byref(r))
                found.append((r.right * r.bottom, hwnd, pid.value))
            kernel32.CloseHandle(h)
        return True

    user32.EnumWindows(EnumProc(cb), 0)
    if not found:
        return None, None
    found.sort(reverse=True)
    return found[0][1], found[0][2]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def capture():
    """Return (w, h, stride, bgr_bytes) of the game window client area."""
    hwnd, _ = game_hwnd()
    if not hwnd:
        sys.exit("game window not found")
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    w, h = r.right, r.bottom
    sdc = user32.GetDC(0)
    mdc = gdi32.CreateCompatibleDC(sdc)
    bmp = gdi32.CreateCompatibleBitmap(sdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    gdi32.BitBlt(mdc, 0, 0, w, h, sdc, pt.x, pt.y, 0x00CC0020)

    class BMI(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                    ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]
    bmi = BMI(ctypes.sizeof(BMI), w, -h, 1, 24, 0, 0, 0, 0, 0, 0)
    stride = (w * 3 + 3) & ~3
    buf = ctypes.create_string_buffer(stride * h)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bmi), 0)
    gdi32.DeleteObject(bmp); gdi32.DeleteDC(mdc); user32.ReleaseDC(0, sdc)
    return w, h, stride, buf.raw


def shot(out, scale=0.5):
    w, h, stride, raw = capture()

    step = max(1, round(1 / scale))
    ow, oh = w // step, h // step
    out_rows = []
    for y in range(oh):
        row = raw[y * step * stride:y * step * stride + w * 3]
        px = bytearray()
        for x in range(ow):
            b, g, rr = row[x * step * 3:x * step * 3 + 3]
            px += bytes((rr, g, b))
        out_rows.append(bytes(px))
    write_png(out, ow, oh, b"".join(out_rows))
    print(f"saved {out} ({ow}x{oh}, window {w}x{h})")


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


def send_scan(code, up):
    flags = 0x0008 | (0x0002 if up else 0)  # KEYEVENTF_SCANCODE | KEYUP
    if code & 0x80:
        flags |= 0x0001  # EXTENDEDKEY
    inp = INPUT(type=1)
    inp.ki = KEYBDINPUT(0, code & 0x7F, flags, 0, 0)
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def keys(seq, hold=0.08, gap=0.35):
    hwnd, pid = game_hwnd()
    if not hwnd:
        sys.exit("game window not found")
    fg_pid = wt.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(fg_pid))
    if fg_pid.value != pid:
        user32.SetForegroundWindow(hwnd)
        time.sleep(0.5)
        user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(fg_pid))
        if fg_pid.value != pid:
            sys.exit("game window is not in the foreground; not sending keys")
    for k in seq:
        code = SCAN[k]
        send_scan(code, False); time.sleep(hold); send_scan(code, True); time.sleep(gap)
    print("sent", ",".join(seq))


def wait_end(timeout=1800, stall=6.0):
    t0 = time.time(); last_f = None; last_change = time.time(); not_replay = 0
    while time.time() - t0 < timeout:
        s = read_status()
        mode, f = s.get("mode"), s.get("frame")
        if mode is None:  # status.txt caught mid-rewrite
            time.sleep(0.2); continue
        not_replay = not_replay + 1 if mode != "3" else 0
        if not_replay >= 2:
            print(f"end: mode={mode} frame={f}"); return
        if f != last_f:
            last_f, last_change = f, time.time()
        elif time.time() - last_change > stall:
            print(f"end: frame stalled at {f} (mode 3)"); return
        time.sleep(0.5)
    print("timeout")


def end_menu_score():
    """Fraction of bright-cyan pixels on the 'Play Again' highlight bar of the replay-over popup.
    Geometry measured on a 1024x576 client: bar spans x 360..924, y 292..316 (scaled to window)."""
    w, h, stride, raw = capture()
    sx, sy = w / 1280, h / 720
    hits = total = 0
    for y in range(int(292 * sy), int(316 * sy), 2):
        for x in range(int(360 * sx), int(924 * sx), 3):
            b, g, r = raw[y * stride + x * 3:y * stride + x * 3 + 3]
            total += 1
            hits += (b > 170 and g > 140 and r < 140)
    return hits / max(total, 1)


def panel_score():
    """Fraction of dark, low-variance pixels in the popup panel rows between the menu bars
    (x 350..930, y 320..380 and 405..440 @1280x720 physical). The popup panel is a flat dark navy."""
    w, h, stride, raw = capture()
    sx, sy = w / 1280, h / 720
    hits = total = 0
    for (y0, y1) in ((320, 330), (352, 360), (440, 452)):
        for y in range(int(y0 * sy), int(y1 * sy), 2):
            for x in range(int(350 * sx), int(930 * sx), 3):
                b, g, r = raw[y * stride + x * 3:y * stride + x * 3 + 3]
                total += 1
                hits += (r < 45 and g < 70 and b < 95 and b >= r)
    return hits / max(total, 1)


def game_foreground():
    hwnd, pid = game_hwnd()
    if not hwnd:
        return False
    fg = wt.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(fg))
    return fg.value == pid


def menu_open(thresh=0.05, panel=0.50):
    # screen capture only shows the game while it is in front; anything else (a terminal with
    # cyan UI) once read as the popup
    if not game_foreground():
        return False
    # the highlighted bar pulses: take the best of a few quick samples
    best = 0.0
    for _ in range(4):
        best = max(best, end_menu_score())
        if best >= thresh:
            break
        time.sleep(0.12)
    return best >= thresh and panel_score() >= panel


def wait_menu(timeout=1800):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if menu_open():
            # must hold for ~3 s and the popup area must stay still (gameplay moves; the popup
            # sits over an idle win pose)
            ref = region_signature(350, 280, 930, 460)
            ok = True
            for _ in range(3):
                time.sleep(1.0)
                d = sig_diff(ref, region_signature(350, 280, 930, 460))
                if not menu_open() or d > 6.0:
                    ok = False
                    break
            if ok:
                print(f"menu: cyan={end_menu_score():.3f} panel={panel_score():.3f} still={d:.2f}"); return
        time.sleep(2)
    print("timeout"); sys.exit(2)


def region_signature(x0, y0, x1, y1):
    """Coarse grayscale signature of a client-area region (1024x576 coordinates)."""
    w, h, stride, raw = capture()
    sx, sy = w / 1280, h / 720
    sig = []
    for y in range(int(y0 * sy), int(y1 * sy), 4):
        for x in range(int(x0 * sx), int(x1 * sx), 4):
            b, g, r = raw[y * stride + x * 3:y * stride + x * 3 + 3]
            sig.append((b + g + r) // 3)
    return sig


def sig_diff(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / max(len(a), 1)


def mode():
    for _ in range(20):
        m = read_status().get("mode")
        if m is not None:
            return m
        time.sleep(0.1)
    return None


def ring_cleared():
    for _ in range(20):
        r = read_status().get("ring")
        if r is not None:
            return int(r, 16) == 0
        time.sleep(0.1)
    return False


def advance():
    """From the replay-over popup: Return to Battle Log -> Yes -> next entry -> LEFT side -> playing.
    Exit codes: 0 next replay playing, 3 end of list, 4 unexpected state (stop and ask)."""
    if not menu_open():
        print("not on the replay-over popup"); sys.exit(4)
    keys(["down"]); keys(["enter"]); time.sleep(0.8)
    keys(["left"]); keys(["enter"])
    for _ in range(40):  # back to the Battle Log: fight state is torn down (mode stays 3 in menus)
        time.sleep(0.5)
        if ring_cleared():
            break
    else:
        print("fight state still loaded after Return to Battle Log"); sys.exit(4)
    time.sleep(2.0)
    next_from_list()


def next_from_list():
    """On the Battle Log with the last played entry highlighted: move down and start it."""
    before = region_signature(90, 150, 970, 576)  # list area
    keys(["down"]); time.sleep(1.0)
    after = region_signature(90, 150, 970, 576)
    d = sig_diff(before, after)
    if d < 2.0:
        print(f"end of list (highlight did not move, diff={d:.2f})"); sys.exit(3)
    keys(["enter"]); time.sleep(1.2)
    keys(["enter"])  # side dialog: LEFT is preselected
    for _ in range(120):
        time.sleep(0.5)
        if not ring_cleared():
            print(f"next replay playing (list diff={d:.2f})"); return
    print("replay did not start"); sys.exit(4)


def wait_start(timeout=60):
    t0 = time.time(); prev = None
    while time.time() - t0 < timeout:
        s = read_status()
        if s.get("mode") == "3" and s.get("frame") not in (None, "0") and prev not in (None, s.get("frame")):
            print(f"playing: frame={s.get('frame')}"); return
        prev = s.get("frame")
        time.sleep(0.5)
    print("not started"); sys.exit(2)


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "shot":
        scale = float(sys.argv[sys.argv.index("--scale") + 1]) if "--scale" in sys.argv else 0.5
        shot(sys.argv[2], scale)
    elif cmd == "keys":
        keys(sys.argv[2].split(","))
    elif cmd == "wait-end":
        wait_end(float(sys.argv[sys.argv.index("--timeout") + 1]) if "--timeout" in sys.argv else 1800)
    elif cmd == "advance":
        advance()
    elif cmd == "next":
        next_from_list()
    elif cmd == "menu-score":
        print(f"cyan={end_menu_score():.3f} panel={panel_score():.3f} open={menu_open()}")
    elif cmd == "wait-menu":
        wait_menu(float(sys.argv[sys.argv.index("--timeout") + 1]) if "--timeout" in sys.argv else 1800)
    elif cmd == "wait-start":
        wait_start(float(sys.argv[sys.argv.index("--timeout") + 1]) if "--timeout" in sys.argv else 60)
    else:
        sys.exit(__doc__)
