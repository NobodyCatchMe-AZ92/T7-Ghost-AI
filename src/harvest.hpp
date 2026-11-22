// t7ghost data harvester core (v1.4 item 9, 2026-11-21): a read-only recorder for sharing, built into the mod (F1 > Data
// Harvesters (optional)) and still into the standalone harvester DLLs (harvest.cpp, dev only since v1.4). No hooks, no
// input changes: a background thread reads the game's fight-state snapshots and writes them as jsonl, exactly like the
// standalone harvesters did, so the profile pipeline reads the files unchanged.
//
//   LIVE    online matches only (game mode 1: ranked, player match, spectated online matches; author 2026-11-21 - no
//           offline fights at all, so no CPU fights can slip in) -> Win64/t7GhostLIVEplayData/live_<time>.jsonl
//   Replay  every replay played back from the Battle Log -> Win64/t7GhostReplayData/rep_<time>.jsonl; Ctrl+F8 (or the
//           F1 button) on the Battle Log starts / stops the automatic replay navigation (pilot()).
//
// Player names are never written: name-like strings are hashed in-process with a random key that stays on this PC
// (Win64/t7GhostDataRecorder_private/salt.bin, until 2026-11-22 t7GhostHarvest_private - moved over on first use, so a
// player keeps their codes; the mod's own key is a different one).
#pragma once
#include "t7.hpp"
#include "loader.hpp"
#include "names.hpp"
#include <share.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace harvest {

enum Mode { kOff = 0, kLive = 1, kReplay = 2 };
struct Variant { const wchar_t *dir, *prefix; const char *name; uint32_t delay; };
// Online fights roll back: recent snapshots can be re-simulated after late inputs arrive. LIVE writes frames this many
// frames behind the newest, once corrections have landed (the ring holds 8).
inline constexpr Variant kVariants[3] = { { L"", L"", "off", 0 }, { L"t7GhostLIVEplayData", L"live_", "live", 6 },
	{ L"t7GhostReplayData", L"rep_", "replay", 0 } };

inline std::atomic<int> g_mode{ kOff };        // set by the host (mod: F1 / ini; standalone: fixed)
inline std::atomic<bool> g_pilot_on{ false };   // the automatic replay navigation is running
inline std::atomic<bool> g_pilot_req{ false };  // start it (the F1 button)
inline std::atomic<bool> g_recording{ false };  // a file is open (F1 status)
inline std::atomic<int> g_files{ 0 };           // files written this session
inline std::wstring g_win64;                    // .../Binaries/Win64/
inline std::wstring g_data;                     // .../Win64/<variant dir>/ (harvester thread)
inline FILE *g_log = nullptr;                   // <variant dir>/DataRecorder_log.txt (was harvest_log.txt)
inline int g_cur = kOff;                        // the variant the harvester thread runs (its own copy of g_mode)
// Background replay navigation (mod only, author 2026-11-21: the game need not be in front). The mod feeds presses into
// the game's own menu input (exe+0x3ED510 repeat / 0x3ED5F0 pressed bits, the source of every menu, Battle Log and
// dialogs included) and says when a game dialog is up (its per-frame update, exe+0x49D120). Without these (standalone
// harvester) the pilot sends keys with SendInput and watches the window, which needs the game in front.
inline constexpr uint32_t kInUp = 1, kInDown = 2, kInLeft = 4, kInRight = 8, kInConfirm = 0x1000;
inline bool g_focusless = false;                // set by the mod once its menu input hooks are on
inline bool (*g_dialog_up)() = nullptr;         // a game dialog is being updated right now
inline std::atomic<uint32_t> g_inject{ 0 };     // bits to press (pilot -> the mod's input hooks)
inline std::atomic<bool> g_inject_done{ false };
// called by the mod's hooks on the game's menu input: the press lasts one frame (12 ms from its first read); directions
// go to the repeat bits (cursor movement), Confirm to the pressed bits, as the game's own dialogs read them
inline uint32_t inject_bits(bool pressed_getter)
{
	const uint32_t b = g_inject;
	if (!b)
		return 0;
	static LARGE_INTEGER freq{}, t0{};
	static uint32_t t0_for = 0;
	if (!freq.QuadPart)
		QueryPerformanceFrequency(&freq);
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (t0_for != b || !t0.QuadPart)
		t0 = now, t0_for = b;
	if ((now.QuadPart - t0.QuadPart) * 1000 > freq.QuadPart * 12)
	{
		g_inject = 0, t0.QuadPart = 0, t0_for = 0, g_inject_done = true;
		return 0;
	}
	return pressed_getter ? (b & kInConfirm) : (b & (kInUp | kInDown | kInLeft | kInRight));
}

inline void hlog(const char *fmt, ...)
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

inline std::wstring stamp_name()
{
	wchar_t buf[64];
	const time_t t = time(nullptr);
	tm lt;
	localtime_s(&lt, &t);
	wcsftime(buf, 64, L"%Y%m%d_%H%M%S", &lt);
	return g_data + kVariants[g_cur].prefix + buf + L".jsonl";
}

struct Recorder
{
	FILE *f = nullptr;
	std::wstring path;
	uint32_t last_fc = 0;
	uint64_t frames = 0;
	bool probed_late = false;
	int32_t chars[2] = { -1, -1 };
	uint64_t sig = 0;                              // fingerprint: characters, stage, the first 300 frames' moves
	std::wstring last_path; uint64_t last_sig = 0; // of the file closed last (the pilot's end-of-list check)

	void write_probe(const char *when)
	{
		const auto hits = names::probe();
		fprintf(f, "{\"names_probe\":%s,\"side_info\":%s,\"stage\":%d,\"mode\":%d,\"variant\":\"%s\",\"build\":%u,\"when\":\"%s\"}\n",
			names::to_json(hits).c_str(), names::side_info_json().c_str(), t7::read_global(t7::kStageId),
			t7::read_global(t7::kGameMode), kVariants[g_cur].name, t7::exe_stamp(), when);
	}

	void open()
	{
		path = stamp_name();
		f = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
		if (!f)
		{
			hlog("could not create %ls", path.c_str());
			return;
		}
		last_fc = 0, frames = 0, probed_late = false;
		sig = 1469598103934665603ull ^ (uint64_t(uint32_t(chars[0])) << 32 | uint32_t(chars[1])) ^ uint64_t(t7::read_global(t7::kStageId)) << 16;
		g_recording = true;
		hlog("recording %ls (chars %d vs %d, stage %d, mode %d)", path.c_str() + g_data.size(), chars[0], chars[1],
			t7::read_global(t7::kStageId), t7::read_global(t7::kGameMode));
		write_probe("open");
	}

	void close()
	{
		if (!f)
			return;
		fclose(f);
		f = nullptr;
		g_recording = false;
		last_path = path, last_sig = frames >= 300 ? sig : 0;
		if (frames == 0) // loading screen (placeholder characters): nothing recorded, no file
		{
			DeleteFileW(path.c_str());
			hlog("  (empty, removed)");
			return;
		}
		++g_files;
		hlog("  done: %llu frames", frames);
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
		if (frames < 300)
			for (int p = 0; p < 2; ++p)
			{
				int32_t mv = 0;
				t7::rd(blk + p * t7::kP2 + 0x350, mv);
				sig = (sig ^ uint32_t(mv)) * 1099511628211ull;
			}
		++frames;
	}

	// every ring copy newer than the last written frame (and at least the variant's delay behind the newest)
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
		const uint32_t delay = kVariants[g_cur].delay;
		if (newest < delay)
			return;
		const uint32_t upto = newest - delay;
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
};
inline Recorder g_rec;

// ---------------------------------------------------------------- automatic replay navigation (Replay)
// Port of tools/replay_pilot.py. Start on the Battle Log with the first replay to harvest highlighted; Ctrl+F8 (or the F1
// button) starts it, Ctrl+F8 again stops. Loop: Enter, Enter (side dialog: left preselected) -> wait for the "replay
// over" popup (cyan "Play Again" bar on a dark panel, still for 3 s; screen capture of the game window) -> Down, Enter
// (Return to Battle Log), Left, Enter (Yes) -> wait until the fight state is torn down -> Down (if the highlight doesn't
// move: end of the list, stop) -> repeat. Keys are only sent while the game window is in the foreground.
inline HWND game_window()
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

inline bool game_foreground()
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

inline bool capture(Shot &s)
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
inline float end_menu_score(const Shot &s)
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
inline float panel_score(const Shot &s)
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
inline std::vector<int> signature(const Shot &s, int x0, int y0, int x1, int y1)
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

inline float sig_diff(const std::vector<int> &a, const std::vector<int> &b)
{
	if (a.empty() || a.size() != b.size())
		return 1000.0f;
	double d = 0;
	for (size_t i = 0; i < a.size(); ++i)
		d += std::abs(a[i] - b[i]);
	return float(d / a.size());
}

// Ctrl+F8 newly pressed while the game is in front (edge detection, shared by every wait)
inline bool hotkey_pressed()
{
	static bool was_down = false;
	const bool down = (GetAsyncKeyState(VK_F8) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && game_foreground();
	const bool pressed = down && !was_down;
	was_down = down;
	return pressed;
}

inline void stop_pilot(const char *why)
{
	if (g_pilot_on.exchange(false))
	{
		hlog("auto replay: stopped (%s)", why);
		MessageBeep(MB_ICONHAND);
	}
}

// waits that end early when the pilot is switched off (Ctrl+F8 again, or the Replay harvester unticked)
inline bool pilot_sleep(int ms)
{
	for (int t = 0; t < ms; t += 50)
	{
		if (g_pilot_on && hotkey_pressed())
			stop_pilot("Ctrl+F8");
		if (g_pilot_on && g_mode != kReplay)
			stop_pilot("the Replay harvester was switched off");
		if (!g_pilot_on)
			return false;
		Sleep(50);
	}
	return g_pilot_on;
}

inline constexpr WORD kEnter = 0x1C, kDown = 0x50, kLeft = 0x4B; // arrows are extended keys
// one menu press through the mod's input hooks (any window in front); false = the pilot was switched off
inline bool press(uint32_t bits)
{
	g_inject_done = false;
	g_inject = bits;
	for (int t = 0; t < 3000 && !g_inject_done; t += 50)
		if (!pilot_sleep(50))
			return g_inject = 0, false;
	if (!g_inject_done)
	{
		g_inject = 0;
		hlog("auto replay: the game did not read a press (0x%X) for 3 s", bits);
		return pilot_sleep(1000);
	}
	return pilot_sleep(350);
}

// tap DirectInput scancodes into the game; waits while the game is not in the foreground
inline bool key(WORD scan, bool extended = false)
{
	if (g_focusless) // the same keys as menu presses
		return press(scan == kEnter ? kInConfirm : scan == kDown ? kInDown : scan == kLeft ? kInLeft : 0);
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

inline bool menu_open(Shot &s)
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

// the "replay over" popup is up and has stayed still for ~3 s (gameplay moves; the popup sits over an idle win pose)
inline bool wait_replay_over()
{
	if (g_focusless && g_dialog_up)
	{
		// the "replay over" popup = a game dialog updating during replay playback, steadily for 1.5 s
		for (int up_ms = 0;;)
		{
			if (!pilot_sleep(100))
				return false;
			const bool up = t7::read_global(t7::kGameMode) == t7::kModeReplay && g_dialog_up();
			up_ms = up ? up_ms + 100 : 0;
			if (up_ms >= 1500)
				return hlog("auto replay: replay over (dialog up)"), true;
		}
	}
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

inline bool fight_loaded() { return t7::ring_base() != 0; }

inline bool wait_until(bool want_loaded, int timeout_ms)
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

// Enter on the highlighted entry, Enter on the side dialog (left), until the replay is playing
inline bool start_highlighted()
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

inline DWORD WINAPI pilot(void *)
{
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); // physical pixels
	for (;;)
	{
		const bool by_key = hotkey_pressed(), by_button = g_pilot_req.exchange(false);
		if (g_mode != kReplay || (!by_key && !by_button))
		{
			Sleep(30);
			continue;
		}
		g_pilot_on = true;
		MessageBeep(MB_OK);
		hlog("auto replay: started (%s)", by_key ? "Ctrl+F8" : "F1 button");
		// Ctrl+F8 must be released before keys are sent (Ctrl held would change them); after the F1 button the menu
		// needs a moment to close and give the game its keys back
		while ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && pilot_sleep(50))
			;
		if (by_button)
			pilot_sleep(700);
		int played = 0;
		std::vector<uint64_t> seen; // fingerprints of the replays played this run (background end-of-list check)
		bool ok = g_pilot_on && start_highlighted();
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
			if (g_focusless)
			{
				// a replay played before in this run: the list wrapped (or did not move) - its new copy goes, stop
				const uint64_t sig = g_rec.last_sig;
				if (sig && std::find(seen.begin(), seen.end(), sig) != seen.end())
				{
					DeleteFileW(g_rec.last_path.c_str());
					hlog("auto replay: that replay was already harvested in this run (end of the list) - its copy removed");
					stop_pilot("end of the list");
					break;
				}
				if (sig)
					seen.push_back(sig);
				if (!key(kDown, true) || !pilot_sleep(1000))
					break;
				ok = start_highlighted();
				continue;
			}
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
				hlog("auto replay: end of the list after %d replays", played);
				stop_pilot("end of the list");
				break;
			}
			ok = start_highlighted();
		}
		if (g_pilot_on)
			stop_pilot("stopped");
		hlog("auto replay: %d replays played this run", played);
	}
}

// ---------------------------------------------------------------- LIVE: is a fight running?
// A live fight = the snapshot frame counter keeps advancing outside replays. Starts after 30 advancing frames, ends 5 s
// after the counter stops (results / menus / loading).
inline bool fight_running()
{
	static uint32_t prev = 0;
	static int advancing = 0;
	static ULONGLONG last_advance = 0;
	const int mode = t7::read_global(t7::kGameMode);
	uint32_t fc = 0;
	const bool ring = t7::ring_base() != 0 && t7::newest_block(&fc);
	static int last_mode = -12345;
	static ULONGLONG next_diag = 0;
	if (mode != last_mode || (GetTickCount64() >= next_diag && !g_rec.f))
	{
		if (mode != last_mode)
			hlog("game mode %d (fight snapshots %s, frame %u)", mode, ring ? "yes" : "no", fc);
		last_mode = mode;
		next_diag = GetTickCount64() + 60000;
	}
	// online matches only (author 2026-11-21): offline fights - against the CPU, Practice, offline VS - are never recorded
	if (mode != t7::kModeOnline || !ring)
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

inline void tick()
{
	bool active = false;
	if (g_cur == kLive)
		active = fight_running();
	else
		active = t7::read_global(t7::kGameMode) == t7::kModeReplay && t7::ring_base() != 0;
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

// switches the harvester thread to the variant the host chose (closes the file and log of the old one)
inline void switch_to(int m)
{
	g_rec.close();
	if (g_log)
	{
		hlog("%s Data Recorder switched off", kVariants[g_cur].name);
		fclose(g_log), g_log = nullptr;
	}
	g_cur = m;
	if (m == kOff)
		return;
	static bool keyed = false;
	if (!keyed)
	{
		// renamed 2026-11-22 (author: "Data Recorders"): an existing key folder is moved, never replaced - the same key
		// keeps a player's name codes the same as in the data they sent before
		const std::wstring old_key = g_win64 + L"t7GhostHarvest_private", new_key = g_win64 + L"t7GhostDataRecorder_private";
		if (GetFileAttributesW(new_key.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(old_key.c_str()) != INVALID_FILE_ATTRIBUTES)
			MoveFileW(old_key.c_str(), new_key.c_str());
		keyed = true, names::load_key(g_win64 + L"t7GhostDataRecorder_"); // -> Win64/t7GhostDataRecorder_private/salt.bin
	}
	g_data = g_win64 + kVariants[m].dir + L"\\";
	CreateDirectoryW(g_data.c_str(), nullptr);
	if (GetFileAttributesW((g_data + L"DataRecorder_log.txt").c_str()) == INVALID_FILE_ATTRIBUTES)
		MoveFileW((g_data + L"harvest_log.txt").c_str(), (g_data + L"DataRecorder_log.txt").c_str()); // the old name, kept going
	g_log = _wfsopen((g_data + L"DataRecorder_log.txt").c_str(), L"ab", _SH_DENYWR);
	hlog("t7ghost %s Data Recorder started", kVariants[m].name);
}

inline DWORD WINAPI worker(void *)
{
	names::g_harvest_thread = true; // this thread hashes names with the harvester's key
	loader::time_begin_period(1);
	for (;;)
	{
		const int m = g_mode;
		if (m != g_cur)
			switch_to(m);
		if (g_cur == kOff)
		{
			Sleep(50);
			continue;
		}
		tick();
		Sleep(2);
	}
}

// starts the harvester thread and the replay navigation thread (both idle while g_mode is off). win64 = the game's
// Binaries/Win64 folder with a trailing backslash. False on another game build: then nothing is recorded.
inline bool start(const std::wstring &win64)
{
	g_win64 = win64;
	if (t7::exe_stamp() != t7::kExeStamp)
		return false;
	CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr));
	CloseHandle(CreateThread(nullptr, 0, pilot, nullptr, 0, nullptr));
	return true;
}

} // namespace harvest
