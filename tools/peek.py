"""Read-only peek at fields of the live player objects in the running game (offline use only).

  peek.py watch OFF[,OFF...] [--secs S]   print P1/P2 values of int32 fields whenever they change
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
LIVE_P1, LIVE_P2 = 0x34EA7D0, 0x34EDE40


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("GlblcntUsage", wt.DWORD), ("ProccntUsage", wt.DWORD), ("modBaseAddr", ctypes.c_void_p),
                ("modBaseSize", wt.DWORD), ("hModule", wt.HMODULE), ("szModule", ctypes.c_wchar * 256),
                ("szExePath", ctypes.c_wchar * 260)]


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD),
                ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", wt.LONG), ("dwFlags", wt.DWORD),
                ("szExeFile", ctypes.c_wchar * 260)]


def find_game():
    snap = k32.CreateToolhelp32Snapshot(0x2, 0)
    pe = PROCESSENTRY32W(dwSize=ctypes.sizeof(PROCESSENTRY32W))
    pid = None
    ok = k32.Process32FirstW(snap, ctypes.byref(pe))
    while ok:
        if pe.szExeFile.lower() == "tekkengame-win64-shipping.exe":
            pid = pe.th32ProcessID
        ok = k32.Process32NextW(snap, ctypes.byref(pe))
    k32.CloseHandle(snap)
    if not pid:
        sys.exit("game not running")
    snap = k32.CreateToolhelp32Snapshot(0x8 | 0x10, pid)
    me = MODULEENTRY32W(dwSize=ctypes.sizeof(MODULEENTRY32W))
    k32.Module32FirstW(snap, ctypes.byref(me))
    base = me.modBaseAddr
    k32.CloseHandle(snap)
    h = k32.OpenProcess(0x10, False, pid)  # PROCESS_VM_READ only
    return h, base


def read_i32(h, addr):
    v = ctypes.c_int32()
    n = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(addr), ctypes.byref(v), 4, ctypes.byref(n)):
        return None
    return v.value


def watch(offs, secs):
    h, base = find_game()
    last = None
    t0 = time.time()
    while time.time() - t0 < secs:
        row = []
        for obj in (LIVE_P1, LIVE_P2):
            row.append(tuple(read_i32(h, base + obj + o) for o in offs))
        if row != last:
            print(f"{time.time() - t0:6.2f}s  " + "  |  ".join(
                f"P{i + 1} " + " ".join(f"+0x{o:X}={v}" for o, v in zip(offs, r)) for i, r in enumerate(row)))
            last = row
        time.sleep(0.05)


if __name__ == "__main__":
    if sys.argv[1] == "watch":
        offs = [int(x, 16) for x in sys.argv[2].split(",")]
        secs = float(sys.argv[sys.argv.index("--secs") + 1]) if "--secs" in sys.argv else 30
        watch(offs, secs)
