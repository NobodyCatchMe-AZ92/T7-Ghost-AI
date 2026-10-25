// t7ghost harvester: a read-only recorder for sharing. Loaded as version.dll from
// TekkenGame/Binaries/Win64 (next to the game exe). No hooks, no input changes, no AI: a background
// thread reads the game's fight-state snapshots and writes them as jsonl, exactly like the
// t7ghost dev build's recorder, so the existing profile pipeline reads the files unchanged.
//
// Replay build: Ctrl+F8 on the Battle Log starts/stops automatic replay navigation (see pilot()).
//
// Two builds of this file:
//   default        REPLAY harvester: records every replay played back from the Battle Log
//                  -> Win64/t7GhostReplayData/rep_<time>.jsonl (one file per replay)
//   /DT7_LIVE      LIVE harvester: records every fight that is not a replay, as played or spectated (online,
//                  offline vs), not replays or Practice -> Win64/t7GhostLIVEplayData/live_<time>.jsonl
//
// Player names are never written: name-like strings are hashed in-process with a random key that
// stays on this PC (Win64/t7GhostHarvest_private/salt.bin - do not share that folder).
#include "t7.hpp"
#include "loader.hpp"
#include "names.hpp"
#include <share.h>
#include <timeapi.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

namespace {

#ifdef T7_LIVE
constexpr const wchar_t *kDataDir = L"t7GhostLIVEplayData";
constexpr const wchar_t *kPrefix = L"live_";
constexpr const char *kVariant = "live";
// Online fights roll back: recent snapshots can be re-simulated after late inputs arrive. Frames
// are written this many frames behind the newest, once corrections have landed (ring holds 8).
constexpr uint32_t kDelay = 6;
#else
constexpr const wchar_t *kDataDir = L"t7GhostReplayData";
constexpr const wchar_t *kPrefix = L"rep_";
constexpr const char *kVariant = "replay";
constexpr uint32_t kDelay = 0;
#endif

std::wstring g_win64; // .../Binaries/Win64/
std::wstring g_data;  // .../Win64/<data dir>/
FILE *g_log = nullptr;

void logf(const char *fmt, ...)
{
	if (!g_log)
		return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(g_log, "%04d-%02d-%02d %02d:%02d:%02d ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fputc('\n', g_log);
	fflush(g_log);
}

std::wstring stamp_name()
{
	wchar_t buf[64];
	const time_t t = time(nullptr);
	tm lt;
	localtime_s(&lt, &t);
	wcsftime(buf, 64, L"%Y%m%d_%H%M%S", &lt);
	return g_data + kPrefix + buf + L".jsonl";
}

struct Recorder
{
	FILE *f = nullptr;
	std::wstring path;
	uint32_t last_fc = 0;
	uint64_t frames = 0, files = 0;
	bool probed_late = false;
	int32_t chars[2] = { -1, -1 };

	void write_probe(const char *when)
	{
		const auto hits = names::probe();
		fprintf(f, "{\"names_probe\":%s,\"side_info\":%s,\"stage\":%d,\"mode\":%d,\"variant\":\"%s\",\"build\":%u,\"when\":\"%s\"}\n",
			names::to_json(hits).c_str(), names::side_info_json().c_str(), t7::read_global(t7::kStageId),
			t7::read_global(t7::kGameMode), kVariant, t7::exe_stamp(), when);
	}

	void open()
	{
		path = stamp_name();
		f = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
		if (!f)
		{
			logf("could not create %ls", path.c_str());
			return;
		}
		last_fc = 0, frames = 0, probed_late = false;
		++files;
		logf("recording %ls (chars %d vs %d, stage %d, mode %d)", path.c_str() + g_data.size(), chars[0], chars[1],
			t7::read_global(t7::kStageId), t7::read_global(t7::kGameMode));
		write_probe("open");
	}

	void close()
	{
		if (!f)
			return;
		fclose(f);
		f = nullptr;
		if (frames == 0) // loading screen (placeholder characters): nothing recorded, no file
		{
			DeleteFileW(path.c_str());
			logf("  (empty, removed)");
			return;
		}
		logf("  done: %llu frames", frames);
	}

	void write_frame(uintptr_t blk, uint32_t fc)
	{
		fprintf(f, "{\"f\":%u", fc);
		for (int p = 0; p < 2; ++p)
		{
			fprintf(f, ",\"p%d\":{", p + 1);
			const uintptr_t pl = blk + p * t7::kP2;
			bool first = true;
			for (const auto &fd : t7::kFields)
			{
				const double v = t7::read_field(pl, fd);
				fprintf(f, fd.ty == t7::Ty::f32 ? "%s\"%s\":%.3f" : "%s\"%s\":%.0f", first ? "" : ",", fd.name, v);
				first = false;
			}
			fputc('}', f);
		}
		fputs("}\n", f);
		++frames;
	}

	// every ring copy newer than the last written frame (and at least kDelay behind the newest)
	void poll()
	{
		if (!f)
			return;
		const uintptr_t r = t7::ring_base();
		uint32_t newest = 0;
		if (!r || !t7::newest_block(&newest))
			return;
		if (last_fc && newest + 30 < last_fc) // new round: the frame counter restarts
		{
			fprintf(f, "{\"reset\":1,\"from\":%u,\"to\":%u}\n", last_fc, newest);
			last_fc = 0;
		}
		if (newest < kDelay)
			return;
		const uint32_t upto = newest - kDelay;
		std::pair<uint32_t, uintptr_t> copies[t7::kRingCount];
		int n = 0;
		for (int i = 0; i < t7::kRingCount; ++i)
		{
			uint32_t fc;
			const uintptr_t blk = r + i * t7::kRingStride;
			if (t7::rd(blk + t7::kFrameCount, fc) && fc > last_fc && fc <= upto)
				copies[n++] = { fc, blk };
		}
		std::sort(copies, copies + n);
		for (int i = 0; i < n; ++i)
		{
			write_frame(copies[i].second, copies[i].first);
			last_fc = copies[i].first;
			if (!probed_late && last_fc >= 300)
				probed_late = true, write_probe("f300");
		}
		if (n)
			fflush(f);
	}

	// current character pair from the newest snapshot
	bool read_chars(int32_t c[2])
	{
		c[0] = c[1] = -1;
		const uintptr_t blk = t7::newest_block();
		return blk && t7::rd(blk + 0xDC, c[0]) && t7::rd(blk + t7::kP2 + 0xDC, c[1]);
	}
} g_rec;

#ifndef T7_LIVE
// ---------------------------------------------------------------- auto replay pilot (Ctrl+F8)
// Port of tools/replay_pilot.py. Start on the Battle Log with the first replay to harvest
// highlighted; Ctrl+F8 again stops. Loop: Enter, Enter (side dialog: left preselected) -> wait for
// the "replay over" popup (cyan "Play Again" bar on a dark panel, still for 3 s; screen capture of
// the game window) -> Down, Enter (Return to Battle Log), Left, Enter (Yes) -> wait until the fight
// state is torn down -> Down (if the highlight doesn't move: end of the list, stop) -> repeat.
// Keys are only sent while the game window is in the foreground; otherwise the pilot waits.
std::atomic<bool> g_pilot_on = false;

HWND game_window()
{
	struct Find { DWORD pid; HWND best; LONG area; } fd{ GetCurrentProcessId(), nullptr, 0 };
	EnumWindows([](HWND h, LPARAM lp) -> BOOL {
		auto *f = reinterpret_cast<Find *>(lp);
		DWORD pid = 0;
		GetWindowThreadProcessId(h, &pid);
		RECT r;
		if (pid == f->pid && IsWindowVisible(h) && GetClientRect(h, &r) && r.right * r.bottom > f->area)
			f->best = h, f->area = r.right * r.bottom;
		return TRUE;
	}, LPARAM(&fd));
	return fd.best;
}

bool game_foreground()
{
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	return pid == GetCurrentProcessId();
}

// game window client area as top-down BGR (24 bit, rows padded to 4 bytes)
struct Shot
{
	int w = 0, h = 0, stride = 0;
	std::vector<uint8_t> px;
	const uint8_t *at(int x, int y) const { return &px[size_t(y) * stride + size_t(x) * 3]; }
};

bool capture(Shot &s)
{
	const HWND hwnd = game_window();
	RECT r;
	if (!hwnd || !GetClientRect(hwnd, &r) || r.right <= 0 || r.bottom <= 0)
		return false;
	POINT pt{ 0, 0 };
	ClientToScreen(hwnd, &pt);
	s.w = r.right, s.h = r.bottom, s.stride = (s.w * 3 + 3) & ~3;
	s.px.assign(size_t(s.stride) * s.h, 0);
	HDC sdc = GetDC(nullptr);
	HDC mdc = CreateCompatibleDC(sdc);
	HBITMAP bmp = CreateCompatibleBitmap(sdc, s.w, s.h);
	HGDIOBJ old = SelectObject(mdc, bmp);
	BitBlt(mdc, 0, 0, s.w, s.h, sdc, pt.x, pt.y, SRCCOPY);
	BITMAPINFOHEADER bi{ sizeof bi, s.w, -s.h, 1, 24, BI_RGB };
	const int rows = GetDIBits(mdc, bmp, 0, s.h, s.px.data(), reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS);
	SelectObject(mdc, old);
	DeleteObject(bmp);
	DeleteDC(mdc);
	ReleaseDC(nullptr, sdc);
	return rows == s.h;
}

// fraction of bright-cyan pixels on the "Play Again" bar (x 360..924, y 292..316 @1280x720)
float end_menu_score(const Shot &s)
{
	const float sx = s.w / 1280.0f, sy = s.h / 720.0f;
	int hits = 0, total = 0;
	for (int y = int(292 * sy); y < int(316 * sy); y += 2)
		for (int x = int(360 * sx); x < int(924 * sx); x += 3)
		{
			const uint8_t *p = s.at(x, y); // b g r
			++total;
			hits += p[0] > 170 && p[1] > 140 && p[2] < 140;
		}
	return total ? float(hits) / total : 0.0f;
}

// fraction of flat dark-navy pixels in the popup panel rows
float panel_score(const Shot &s)
{
	const float sx = s.w / 1280.0f, sy = s.h / 720.0f;
	int hits = 0, total = 0;
	const int rows[3][2] = { { 320, 330 }, { 352, 360 }, { 440, 452 } };
	for (const auto &rr : rows)
		for (int y = int(rr[0] * sy); y < int(rr[1] * sy); y += 2)
			for (int x = int(350 * sx); x < int(930 * sx); x += 3)
			{
				const uint8_t *p = s.at(x, y);
				++total;
				hits += p[2] < 45 && p[1] < 70 && p[0] < 95 && p[0] >= p[2];
			}
	return total ? float(hits) / total : 0.0f;
}

// coarse grayscale signature of a region (1280x720 coordinates)
std::vector<int> signature(const Shot &s, int x0, int y0, int x1, int y1)
{
	const float sx = s.w / 1280.0f, sy = s.h / 720.0f;
	std::vector<int> sig;
	for (int y = int(y0 * sy); y < int(y1 * sy) && y < s.h; y += 4)
		for (int x = int(x0 * sx); x < int(x1 * sx) && x < s.w; x += 4)
		{
			const uint8_t *p = s.at(x, y);
			sig.push_back((p[0] + p[1] + p[2]) / 3);
		}
	return sig;
}

float sig_diff(const std::vector<int> &a, const std::vector<int> &b)
{
	if (a.empty() || a.size() != b.size())
		return 1000.0f;
	double d = 0;
	for (size_t i = 0; i < a.size(); ++i)
		d += std::abs(a[i] - b[i]);
	return float(d / a.size());
}

// Ctrl+F8 newly pressed while the game is in front (edge detection, shared by every wait)
bool hotkey_pressed()
{
	static bool was_down = false;
	const bool down = (GetAsyncKeyState(VK_F8) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && game_foreground();
	const bool pressed = down && !was_down;
	was_down = down;
	return pressed;
}

void stop_pilot(const char *why);

// waits that end early when the pilot is switched off (Ctrl+F8 again); false = switched off
bool pilot_sleep(int ms)
{
	for (int t = 0; t < ms; t += 50)
	{
		if (g_pilot_on && hotkey_pressed())
			stop_pilot("Ctrl+F8");
		if (!g_pilot_on)
			return false;
		Sleep(50);
	}
	return g_pilot_on;
}

// tap DirectInput scancodes into the game; waits while the game is not in the foreground
bool key(WORD scan, bool extended = false)
{
	while (!game_foreground())
		if (!pilot_sleep(500))
			return false;
	INPUT in[2] = {};
	for (int i = 0; i < 2; ++i)
	{
		in[i].type = INPUT_KEYBOARD;
		in[i].ki.wScan = scan;
		in[i].ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (i ? KEYEVENTF_KEYUP : 0);
	}
	SendInput(1, &in[0], sizeof(INPUT));
	if (!pilot_sleep(80))
		return false;
	SendInput(1, &in[1], sizeof(INPUT));
	return pilot_sleep(350);
}
constexpr WORD kEnter = 0x1C, kDown = 0x50, kLeft = 0x4B; // arrows are extended keys

bool menu_open(Shot &s)
{
	if (!game_foreground())
		return false;
	float best = 0;
	for (int i = 0; i < 4 && best < 0.05f; ++i) // the highlight bar pulses
	{
		if (!capture(s))
			return false;
		best = std::max(best, end_menu_score(s));
		if (best < 0.05f && !pilot_sleep(120))
			return false;
	}
	return best >= 0.05f && panel_score(s) >= 0.50f;
}

// the "replay over" popup is up and has stayed still for ~3 s (gameplay moves; the popup sits over
// an idle win pose)
bool wait_replay_over()
{
	Shot s;
	for (;;)
	{
		if (menu_open(s))
		{
			const auto ref = signature(s, 350, 280, 930, 460);
			bool ok = true;
			for (int i = 0; i < 3 && ok; ++i)
			{
				if (!pilot_sleep(1000))
					return false;
				ok = menu_open(s) && sig_diff(ref, signature(s, 350, 280, 930, 460)) <= 6.0f;
			}
			if (ok)
				return true;
		}
		if (!pilot_sleep(2000))
			return false;
	}
}

bool fight_loaded() { return t7::ring_base() != 0; }

bool wait_until(bool want_loaded, int timeout_ms)
{
	for (int t = 0; t < timeout_ms; t += 500)
	{
		if (fight_loaded() == want_loaded)
			return true;
		if (!pilot_sleep(500))
			return false;
	}
	return false;
}

void stop_pilot(const char *why)
{
	if (g_pilot_on.exchange(false))
	{
		logf("auto replay: stopped (%s)", why);
		MessageBeep(MB_ICONHAND);
	}
}

// Enter on the highlighted entry, Enter on the side dialog (left), until the replay is playing
bool start_highlighted()
{
	if (!key(kEnter) || !pilot_sleep(1200) || !key(kEnter))
		return false;
	if (!wait_until(true, 60000))
	{
		stop_pilot("the replay did not start");
		return false;
	}
	return true;
}

DWORD WINAPI pilot(void *)
{
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); // physical pixels
	for (;;)
	{
		if (!hotkey_pressed())
		{
			Sleep(30);
			continue;
		}
		g_pilot_on = true;
		MessageBeep(MB_OK);
		logf("auto replay: started");
		// Ctrl+F8 must be released before keys are sent (Ctrl held would change them)
		while ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && pilot_sleep(50))
			;
		int played = 0;
		bool ok = start_highlighted();
		while (ok && g_pilot_on)
		{
			++played;
			if (!wait_replay_over())
				break;
			// Return to Battle Log -> Yes
			if (!key(kDown, true) || !key(kEnter) || !pilot_sleep(800) || !key(kLeft, true) || !key(kEnter))
				break;
			if (!wait_until(false, 20000))
			{
				stop_pilot("still in the replay after Return to Battle Log");
				break;
			}
			if (!pilot_sleep(2000))
				break;
			// next entry; if the list didn't move, that was the last replay
			Shot s;
			if (!capture(s))
			{
				stop_pilot("cannot capture the game window");
				break;
			}
			const auto before = signature(s, 90, 150, 970, 576);
			if (!key(kDown, true) || !pilot_sleep(1000) || !capture(s))
				break;
			if (sig_diff(before, signature(s, 90, 150, 970, 576)) < 2.0f)
			{
				logf("auto replay: end of the list after %d replays", played);
				stop_pilot("end of the list");
				break;
			}
			ok = start_highlighted();
		}
		if (g_pilot_on)
			stop_pilot("stopped");
		logf("auto replay: %d replays played this run", played);
	}
}
#endif

#ifdef T7_LIVE
// A live fight = the snapshot frame counter keeps advancing outside replays and Practice. Starts
// after 30 advancing frames, ends 5 s after the counter stops (results / menus / loading).
bool fight_running()
{
	static uint32_t prev = 0;
	static int advancing = 0;
	static ULONGLONG last_advance = 0;
	const int mode = t7::read_global(t7::kGameMode);
	uint32_t fc = 0;
	const bool ring = t7::ring_base() != 0 && t7::newest_block(&fc);
	// diagnostics: every game-mode change, with whether fight snapshots exist and their frame
	static int last_mode = -12345;
	static ULONGLONG next_diag = 0;
	if (mode != last_mode || (GetTickCount64() >= next_diag && !g_rec.f))
	{
		if (mode != last_mode)
			logf("game mode %d (fight snapshots %s, frame %u)", mode, ring ? "yes" : "no", fc);
		last_mode = mode;
		next_diag = GetTickCount64() + 60000;
	}
	// Practice shares a mode id with offline VS (2), so only replays are excluded here; the mode is
	// in every file's probe rows and Practice sessions are filtered when the data is processed.
	if (mode == t7::kModeReplay || !ring)
	{
		advancing = 0;
		return false;
	}
	const ULONGLONG now = GetTickCount64();
	if (fc != prev)
	{
		advancing = fc > prev ? advancing + 1 : 0; // a restart (new round) counts as advancing again from 0
		last_advance = now;
		prev = fc;
	}
	if (advancing >= 30 && now - last_advance < 5000)
		return true;
	if (now - last_advance >= 5000)
		advancing = 0;
	return g_rec.f && now - last_advance < 5000; // keep the file open through round transitions
}
#endif

void tick()
{
#ifdef T7_LIVE
	const bool active = fight_running();
#else
	const bool active = t7::read_global(t7::kGameMode) == t7::kModeReplay && t7::ring_base() != 0;
#endif
	if (active)
	{
		int32_t c[2];
		if (g_rec.read_chars(c) && g_rec.f && (c[0] != g_rec.chars[0] || c[1] != g_rec.chars[1]))
			g_rec.close(); // next replay / next match with other characters
		g_rec.chars[0] = c[0], g_rec.chars[1] = c[1];
		if (!g_rec.f)
			g_rec.open();
	}
	else if (g_rec.f)
		g_rec.close();
	g_rec.poll();
}

DWORD WINAPI worker(void *)
{
	timeBeginPeriod(1);
	logf("t7ghost %s harvester started", kVariant);
	if (t7::exe_stamp() != t7::kExeStamp)
	{
		logf("unsupported game build 0x%08X (needs the final Steam build 0x%08X): not recording", t7::exe_stamp(), t7::kExeStamp);
		return 0;
	}
	if (!names::load_key(g_win64 + L"t7GhostHarvest_")) // -> Win64/t7GhostHarvest_private/salt.bin
		logf("could not create the private name key: names are not hashed");
	for (;;)
	{
		tick();
		Sleep(2);
	}
}

}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(mod);
		// one copy per process (the mod or a harvester, under dinput8.dll or version.dll); an old
		// version.dll of ours next to this dinput8.dll would run too - stay off and say so
		if (!loader::first_in_process())
			return TRUE;
		if (loader::old_version_dll_next_to(mod))
		{
			loader::message(loader::kOldVersionDll);
			return TRUE;
		}
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(mod, path, MAX_PATH);
		g_win64 = path;
		g_win64 = g_win64.substr(0, g_win64.find_last_of(L"\\/") + 1);
		g_data = g_win64 + kDataDir + L"\\";
		CreateDirectoryW(g_data.c_str(), nullptr);
		g_log = _wfsopen((g_data + L"harvest_log.txt").c_str(), L"ab", _SH_DENYWR);
		CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr));
#ifndef T7_LIVE
		if (t7::exe_stamp() == t7::kExeStamp)
			CloseHandle(CreateThread(nullptr, 0, pilot, nullptr, 0, nullptr));
#endif
	}
	return TRUE;
}
