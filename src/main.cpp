// t7ghost dev probe. Loaded as version.dll from TekkenGame/Binaries/Win64.
// Everything runs on one background thread; DllMain only starts it.
//
// Replays are recorded automatically while game mode = replay -> t7ghost/replays/rep_<time>.jsonl
// Hotkeys (only while the game window is focused):
//   Ctrl+F6  start/stop manual recorder -> t7ghost/rec_<time>.jsonl
//   Ctrl+F12 start/stop combo recording (Practice demonstrations) -> t7ghost/combos/combo_<time>.jsonl
//   Ctrl+F7  settings tracer start/stop -> t7ghost/trace_<time>.log
//   Ctrl+F8  settings tracer marker
//   Ctrl+F9  reload t7ghost.ini [ai] settings and profiles/
//   Ctrl+F10 start/stop write-watch on P2 input fields (hardware breakpoints)
// Live readout is rewritten to t7ghost/status.txt twice a second.
#include "t7.hpp"
#include "loader.hpp"
#include "brain.hpp"
#include "names.hpp"
#include "finder.hpp"
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <fstream>
#include <share.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <intrin.h>
#include <Xinput.h>
#include <timeapi.h>
#include <algorithm>
#include <thread>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

#define T7_VERSION_STR "1.1a" // keep in step with the t7ghost.ini header (tools/write_ini.py)
constexpr const char *kModVersion = T7_VERSION_STR;
std::wstring g_dir; // .../Win64/t7ghost/
FILE *g_log = nullptr;
std::mutex g_log_mx;

void logf(const char *fmt, ...)
{
	std::lock_guard lk(g_log_mx);
	if (!g_log)
		return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(g_log, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fputc('\n', g_log);
	fflush(g_log);
}

std::wstring stamp_name(const wchar_t *prefix, const wchar_t *ext)
{
	wchar_t buf[64];
	const time_t t = time(nullptr);
	tm lt;
	localtime_s(&lt, &t);
	wcsftime(buf, 64, L"%Y%m%d_%H%M%S", &lt);
	return g_dir + prefix + buf + ext;
}

bool game_focused()
{
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	return pid == GetCurrentProcessId();
}

bool hotkey(int vk)
{
	static bool down[256] = {};
	const bool now = (GetAsyncKeyState(vk) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
	const bool fired = now && !down[vk];
	down[vk] = now;
	return fired && game_focused();
}

// ---------------------------------------------------------------- recorder
finder::Finder g_finder; // health/rage field finder, fed while replays are recorded

struct Recorder
{
	FILE *f = nullptr;
	uint32_t last_fc = 0;
	uint64_t frames = 0;
	bool probed_late = false;

	// name hashes only (see names.hpp); written at open and again once the round is running
	void write_probe(const char *when)
	{
		const auto hits = names::probe();
		fprintf(f, "{\"names_probe\":%s,\"side_info\":%s,\"stage\":%d,\"when\":\"%s\"}\n", names::to_json(hits).c_str(),
			names::side_info_json().c_str(), t7::read_global(t7::kStageId), when);
		logf("recorder: name probe (%s): %zu candidate strings", when, hits.size());
	}

	bool automatic = false;
	bool combo = false; // combo lab recording (no name probe; analysis/combos.py reads it)
	int32_t chars[2] = { -1, -1 };

	// Combo lab (2026-10-19): the author demonstrates combo routes in Practice; analysis/combos.py turns the
	// recording into a list of combos (starter, launcher, follow-ups, timing, damage, notation)
	void toggle_combo()
	{
		if (f)
			return toggle();
		CreateDirectoryW((g_dir + L"combos").c_str(), nullptr);
		open(stamp_name(L"combos/combo_", L".jsonl"), false);
		combo = f != nullptr;
	}

	// Records every replay that plays: starts when game mode becomes "replay", stops when it leaves,
	// and starts a new file when the character pair changes (next replay loaded directly).
	void auto_mode()
	{
		const bool replay = t7::read_global(t7::kGameMode) == t7::kModeReplay && t7::ring_base() != 0;
		if (replay)
		{
			int32_t c[2] = { -1, -1 };
			if (const uintptr_t blk = t7::newest_block())
				t7::rd(blk + 0xDC, c[0]), t7::rd(blk + t7::kP2 + 0xDC, c[1]);
			if (f && automatic && (c[0] != chars[0] || c[1] != chars[1]))
				toggle();
			chars[0] = c[0], chars[1] = c[1];
		}
		if (replay && !f)
		{
			CreateDirectoryW((g_dir + L"replays").c_str(), nullptr);
			open(stamp_name(L"replays/rep_", L".jsonl"));
			automatic = true;
		}
		else if (!replay && f && automatic)
			toggle();
	}

	void toggle()
	{
		if (f)
		{
			fclose(f);
			f = nullptr;
			automatic = false;
			combo = false;
			logf("recorder: stopped, %llu frames", frames);
			g_finder.report([](const char *fmt, auto... a) { logf(fmt, a...); });
			return;
		}
		open(stamp_name(L"rec_", L".jsonl"));
	}

	void open(const std::wstring &path, bool probe = true)
	{
		f = _wfsopen(path.c_str(), L"wb", _SH_DENYWR); // readable while the game runs
		if (!f)
			return;
		last_fc = 0;
		frames = 0;
		probed_late = !probe; // combo recordings never store name codes
		logf("recorder: started %ls", path.c_str());
		if (probe)
			write_probe("open");
		else
			fprintf(f, "{\"combo_lab\":1,\"stage\":%d,\"mode\":%d}\n", t7::read_global(t7::kStageId), t7::read_global(t7::kGameMode));
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

	// Emit every ring copy newer than the last written frame, oldest first.
	void poll()
	{
		if (!f)
			return;
		const uintptr_t r = t7::ring_base();
		if (!r)
			return;
		uint32_t newest = 0;
		if (!t7::newest_block(&newest))
			return;
		// frame counter restarts (new round / new replay): mark it and accept the new timeline;
		// stale ring copies from before the restart have fc > newest and are skipped below
		if (last_fc && newest + 30 < last_fc)
		{
			fprintf(f, "{\"reset\":1,\"from\":%u,\"to\":%u}\n", last_fc, newest);
			logf("recorder: frame counter reset %u -> %u", last_fc, newest);
			last_fc = 0;
		}
		std::pair<uint32_t, uintptr_t> copies[t7::kRingCount];
		int n = 0;
		for (int i = 0; i < t7::kRingCount; ++i)
		{
			uint32_t fc;
			const uintptr_t blk = r + i * t7::kRingStride;
			if (t7::rd(blk + t7::kFrameCount, fc) && fc > last_fc && fc <= newest)
				copies[n++] = { fc, blk };
		}
		std::sort(copies, copies + n);
		for (int i = 0; i < n; ++i)
		{
			if (last_fc && copies[i].first != last_fc + 1)
				logf("recorder: gap %u -> %u", last_fc, copies[i].first);
			write_frame(copies[i].second, copies[i].first);
			last_fc = copies[i].first;
			if (!probed_late && last_fc >= 300)
			{
				probed_late = true;
				write_probe("f300");
			}
		}
	}
} g_rec;
std::atomic<bool> g_combo_toggle{ false }; // combo recording on/off asked from the F1 panel (done by the worker)

// ---------------------------------------------------------------- settings tracer
// Ctrl+F7 start/stop, Ctrl+F8 numbered marker. Every 100 ms diffs the exe's .data section and
// logs small-value changes (offsets that change more than 8 times are treated as noise).

struct Tracer
{
	static constexpr uintptr_t kRva = 0x3437000, kSize = 0x6C1928;
	FILE *f = nullptr;
	std::vector<int32_t> prev;
	std::vector<uint16_t> changes;
	ULONGLONG t0 = 0, next = 0;
	int marks = 0;

	void toggle()
	{
		if (f)
		{
			fclose(f);
			f = nullptr;
			logf("tracer: stopped after %d marks", marks);
			return;
		}
		prev.assign(kSize / 4, 0);
		changes.assign(kSize / 4, 0);
		if (!t7::safe_copy(prev.data(), t7::exe_base() + kRva, kSize & ~3ull))
			return logf("tracer: read failed");
		const auto path = stamp_name(L"trace_", L".log");
		f = _wfsopen(path.c_str(), L"wb", _SH_DENYWR); // readable while the game runs
		t0 = GetTickCount64();
		marks = 0;
		logf("tracer: started %ls", path.c_str());
	}

	void mark()
	{
		if (!f)
			return;
		fprintf(f, "t=%llu MARK %d\n", GetTickCount64() - t0, ++marks);
		fflush(f);
		logf("tracer: mark %d", marks);
	}

	void tick()
	{
		if (!f || GetTickCount64() < next)
			return;
		next = GetTickCount64() + 100;
		const auto *cur = reinterpret_cast<const int32_t *>(t7::exe_base() + kRva);
		const ULONGLONG t = GetTickCount64() - t0;
		for (size_t i = 0; i < prev.size(); ++i)
		{
			const int32_t v = cur[i];
			if (v == prev[i])
				continue;
			if (changes[i] < 0xFFFF && ++changes[i] <= 8 && v >= -1 && v <= 1000 && prev[i] >= -1 && prev[i] <= 1000)
				fprintf(f, "t=%llu exe+0x%llX %d -> %d\n", t, kRva + i * 4, prev[i], v);
			prev[i] = v;
		}
	}
} g_tracer;

// ---------------------------------------------------------------- hardware write-watch

std::atomic<bool> g_watching = false;
std::mutex g_hits_mx;
std::map<uintptr_t, std::pair<uint64_t, std::string>> g_hits; // rip -> (count, caller chain)
PVOID g_veh = nullptr;

// Ring slots are only written by the rollback snapshot copy (dst=rdi, src=rbx). Stage A watches
// a ring slot, grabs the copy's source (= live player object), stage B re-arms on that object.
constexpr uintptr_t kRingCopyRip1 = 0x76220B, kRingCopyRip2 = 0x7626E5;
std::atomic<uintptr_t> g_live_src = 0;
std::atomic<int> g_watch_stage = 0;

std::string caller_chain(const CONTEXT *ctx)
{
	CONTEXT c = *ctx;
	std::string out;
	const uintptr_t exe = t7::exe_base();
	for (int depth = 0; depth < 6; ++depth)
	{
		DWORD64 image = 0;
		auto fe = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
		if (!fe)
			break;
		void *handler_data;
		DWORD64 frame;
		RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, fe, &c, &handler_data, &frame, nullptr);
		if (!c.Rip)
			break;
		char buf[48];
		snprintf(buf, sizeof buf, image == exe ? " <- exe+0x%llX" : " <- %p", image == exe ? c.Rip - exe : c.Rip);
		out += buf;
	}
	return out;
}

LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
	if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(ep->ContextRecord->Dr6 & 0xF))
		return EXCEPTION_CONTINUE_SEARCH;
	const uintptr_t rip = ep->ContextRecord->Rip;
	const uintptr_t rel = rip - t7::exe_base();
	if (g_watch_stage == 1 && (rel == kRingCopyRip1 || rel == kRingCopyRip2))
	{
		uintptr_t expected = 0;
		g_live_src.compare_exchange_strong(expected, ep->ContextRecord->Rbx);
	}
	{
		std::lock_guard lk(g_hits_mx);
		auto &h = g_hits[rip];
		if (h.first++ == 0)
		{
			char regs[160];
			const CONTEXT *c = ep->ContextRecord;
			snprintf(regs, sizeof regs, " | rax=%llX rbx=%llX rcx=%llX rdx=%llX rsi=%llX rdi=%llX", c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi);
			h.second = caller_chain(ep->ContextRecord) + regs;
		}
	}
	ep->ContextRecord->Dr6 = 0;
	return EXCEPTION_CONTINUE_EXECUTION;
}

void set_debug_regs(uintptr_t a0, uintptr_t a1, bool on, bool reads = false)
{
	const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	THREADENTRY32 te{ sizeof te };
	int n = 0;
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
	{
		if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
			continue;
		HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
		if (!th)
			continue;
		SuspendThread(th);
		CONTEXT c{};
		c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		if (GetThreadContext(th, &c))
		{
			// slot 0 and 1: break on 4-byte writes (RW=01, LEN=11), or reads and writes (RW=11)
			c.Dr0 = on ? a0 : 0;
			c.Dr1 = on ? a1 : 0;
			const DWORD64 rw = reads ? 3 : 1;
			c.Dr7 = on ? (1 | (1 << 2) | (rw << 16) | (3 << 18) | (rw << 20) | (3 << 22)) : 0;
			c.Dr6 = 0;
			if (SetThreadContext(th, &c))
				++n;
		}
		ResumeThread(th);
		CloseHandle(th);
	}
	CloseHandle(snap);
	logf("watch: debug registers %s on %d threads", on ? "armed" : "cleared", n);
}

void dump_hits(const char *what)
{
	std::lock_guard lk(g_hits_mx);
	logf("watch: %s, %zu distinct writers", what, g_hits.size());
	for (const auto &[rip, h] : g_hits)
		logf("  exe+0x%llX x%llu%s", rip - t7::exe_base(), h.first, h.second.c_str());
	g_hits.clear();
}

// Called from the worker loop: promotes stage A (ring slot) to stage B (live object).
void watch_tick()
{
	if (g_watch_stage != 1 || !g_live_src)
		return;
	const uintptr_t src = g_live_src;
	set_debug_regs(0, 0, false);
	dump_hits("stage A done");
	int32_t ch = -1, mv = -1;
	t7::rd(src + 0xDC, ch);
	t7::rd(src + 0x350, mv);
	logf("watch: live P2 object = %p (char=%d move_id=%d); ring=%p exe=%p", (void *)src, ch, mv, (void *)t7::ring_base(), (void *)t7::exe_base());
	g_watch_stage = 2;
	set_debug_regs(src + 0xDDC, src + 0x1A5C, true);
}

void toggle_watch()
{
	if (g_watching)
	{
		set_debug_regs(0, 0, false);
		g_watching = false;
		g_watch_stage = 0;
		dump_hits("stopped");
		return;
	}
	const uintptr_t blk = t7::newest_block();
	if (!blk)
	{
		logf("watch: no fight loaded");
		return;
	}
	if (!g_veh)
		g_veh = AddVectoredExceptionHandler(1, veh);
	{
		std::lock_guard lk(g_hits_mx);
		g_hits.clear();
	}
	// rage recon (2026-10-19, No Rage): who writes the live players' rage flag +0xC00 (Ctrl+F10 on/off)
	const uintptr_t lp1 = t7::exe_base() + t7::kLiveP1, lp2 = t7::exe_base() + t7::kLiveP2;
	logf("watch: P1 rage state +0x780 @%p, rage used +0x754 @%p", (void *)(lp1 + 0x780), (void *)(lp1 + 0x754));
	g_live_src = 0;
	g_watch_stage = 2;
	set_debug_regs(lp1 + 0x780, lp1 + 0x754, true);
	g_watching = true;
}

// ---------------------------------------------------------------- input decoder hook
// exe+0x747F50: void decode_input(Player *p, int side, uint32_t held, uint32_t pressed)
// Called once per player per frame for humans and CPU alike; writes p+0xDD8/0xDDC/0xDE0 etc.

constexpr uintptr_t kDecodeInput = 0x747F50;
using decode_fn = void(__fastcall *)(uintptr_t, int, uint32_t, uint32_t);
decode_fn g_decode_orig = nullptr;

std::atomic<bool> g_mirror = false; // Ctrl+F11: P2 replays P1's raw input
uint32_t g_raw[2][2] = {};          // [player][held, pressed] of the current frame (game thread only)
uint32_t g_side[2] = {};

// ---------------------------------------------------------------- brain
// Active only in Practice with the dummy set to CPU at Ultra Hard and a profile for P2's character.
// Reloaded by the worker (Ctrl+F9); the game thread only ever reads the published pointer.
std::atomic<ghost::Brain *> g_brain = nullptr;
std::atomic<bool> g_ai_active = false;
std::atomic<int> g_ai_idx = -1; // side the AI drives while active (0 = P1, 1 = P2)

// the Ghost AI values as saved in the ini (Custom Settings); a chosen preset overrides them in memory only
void read_ai_values(ghost::Settings &c)
{
	const std::wstring ini = g_dir + L"t7ghost.ini";
	auto geti = [&](const wchar_t *sec, const wchar_t *key, int def) { return int(GetPrivateProfileIntW(sec, key, def, ini.c_str())); };
	c.reaction_frames = std::clamp(geti(L"ai", L"reaction_frames", 14), 0, 60);
	c.input_read_chance = std::clamp(geti(L"ai", L"input_read_chance", 0), 0, 100);
	c.punish_skill = std::clamp(geti(L"ai", L"punish_proficiency", 50), 0, 100);
	// combo_style 0/1/2 (2026-10-19); an older ini's combos=0 meant no juggle follow-ups: own habits then
	c.combo_style = std::clamp(geti(L"ai", L"combo_style", geti(L"ai", L"combos", 1) ? 1 : 0), 0, 2);
	c.guard_skill = std::clamp(geti(L"ai", L"guard_skill", 100), 0, 100);
	c.decision_skill = std::clamp(geti(L"ai", L"decision_skill", 60), 0, 100);
	c.adapt = std::clamp(geti(L"ai", L"adapt", 70), 0, 100);
	c.throw_break = std::clamp(geti(L"ai", L"throw_break_chance", 0), 0, 100);
	c.wall_awareness = geti(L"ai", L"wall_awareness", 1) != 0;
	// hidden option (author, 2026-10-19): not in the panel or the written ini; "taunts=0" typed into [ai]
	// switches the Ghosts' taunts off
	c.taunts = geti(L"ai", L"taunts", 1) != 0;
	c.ai_preset = std::clamp(geti(L"ai", L"ai_preset", 3), 0, int(std::size(ghost::kAiPresets)) - 1);
	ghost::apply_ai_preset(c, c.ai_preset);
}

ghost::Brain *load_brain()
{
	auto *b = new ghost::Brain();
	const std::wstring ini = g_dir + L"t7ghost.ini";
	auto geti = [&](const wchar_t *sec, const wchar_t *key, int def) { return int(GetPrivateProfileIntW(sec, key, def, ini.c_str())); };
	// "mod_enabled" since v0.5 (ini layout); "enabled" still read for older ini files
	b->cfg.enabled = geti(L"ai", L"mod_enabled", geti(L"ai", L"enabled", 1)) != 0;
	b->cfg.punish = true; // the old on/off switch: now Punishment Proficiency (0 = the player's own punishers)
	read_ai_values(b->cfg); // the Ghost AI sliders (or the chosen Ghost AI Preset)
	b->cfg.arcade = geti(L"ai", L"arcade", 1) != 0;
	b->cfg.treasure = geti(L"ai", L"treasure_battle", 1) != 0;
	b->cfg.ghost_battle = geti(L"ghost_battle", L"enabled", 1) != 0;
	b->cfg.gb_random_stage = geti(L"ghost_battle", L"random_stage", 1) != 0;
	b->cfg.gb_ghost_opponents = geti(L"ghost_battle", L"ghost_opponents", 1) != 0;
	{
		// special fights per kind; an older ini's special_fights=1 turns them all on
		const int all = geti(L"ghost_battle", L"special_fights", 0);
		b->cfg.gb_sp_boss = geti(L"ghost_battle", L"special_boss", all) != 0;
		b->cfg.gb_sp_turbo = geti(L"ghost_battle", L"special_turbo", all) != 0;
		// named after the author's tests (2026-10-25); the first names (special_ghost / _type4 / _type6) still read
		b->cfg.gb_sp_heist = geti(L"ghost_battle", L"special_heist", geti(L"ghost_battle", L"special_ghost", all)) != 0;
		b->cfg.gb_sp_double = geti(L"ghost_battle", L"special_double_damage", geti(L"ghost_battle", L"special_type4", all)) != 0;
		b->cfg.gb_sp_juggle = geti(L"ghost_battle", L"special_aerial_juggle", geti(L"ghost_battle", L"special_type6", all)) != 0;
		b->cfg.gb_force_special = std::clamp(geti(L"ghost_battle", L"force_special", 0), 0, 7); // dev test
		b->cfg.gb_costume_watch_char = geti(L"ghost_battle", L"costume_watch_char", -1); // dev recon
	}
	// costumes 0-3 (2026-10-19, 3 = with presets 2026-10-24); an older ini's default_costumes=1 meant 1P costumes only
	b->cfg.gb_costume_test = std::clamp(geti(L"ghost_battle", L"costume_test", 0), 0, 7);
	b->cfg.gb_costumes = std::clamp(geti(L"ghost_battle", L"costumes", geti(L"ghost_battle", L"default_costumes", 0) ? 1 : 0), 0, 7);
	b->cfg.gb_custom_slot = std::clamp(geti(L"ghost_battle", L"custom_slot", 0), 0, 10); // 2026-10-25, 0 = any
	b->cfg.gb_random_player = geti(L"ghost_battle", L"random_player", 0) != 0;
	b->cfg.gb_end_menu = geti(L"ghost_battle", L"end_menu", 1) != 0;
	b->cfg.gb_native_menu = geti(L"ghost_battle", L"native_menu", 1) != 0;
	b->cfg.gb_rename = geti(L"ghost_battle", L"rename", 1) != 0;
	b->cfg.gb_hide_chests = geti(L"ghost_battle", L"hide_chests", 1) != 0;
	b->cfg.mod_no_rage = geti(L"modifiers", L"no_rage", 0) != 0;
	b->cfg.mod_health = std::clamp(geti(L"modifiers", L"health", 100), 25, 500);
	b->cfg.mod_no_pc = geti(L"modifiers", L"no_power_crush", 0) != 0;
	b->cfg.mod_no_screw = geti(L"modifiers", L"no_screw", 0) != 0;

	// profiles/index.txt lists "<char> <name> <frames>" best-first; [profiles] c<char>=<name> overrides
	const std::wstring dir = g_dir + L"profiles/";
	// Per character ([profiles] c<char>=...): "merged" (default since v0.5.1) = all players as one;
	// "hybrid" = the best profile's style, the other players fill gaps and compete for punishes/combos;
	// or a profile name = that player only.
	std::map<int, std::vector<std::string>> by_char; // best first (index order)
	{
		std::ifstream idx(dir + L"index.txt");
		int cid, frames;
		std::string name;
		while (idx >> cid >> name >> frames)
			by_char[cid].push_back(name);
	}
	for (auto &[cid, names] : by_char)
	{
		wchar_t want_w[64];
		GetPrivateProfileStringW(L"profiles", (L"c" + std::to_wstring(cid)).c_str(), L"merged", want_w, 64, ini.c_str());
		std::string want; // profile names are ascii
		for (const wchar_t *w = want_w; *w; ++w)
			want += char(*w);
		const bool hybrid = want == "hybrid";
		bool merged = want == "merged" || want.empty();
		std::vector<std::string> use;
		if (merged || hybrid)
			use = names;
		else if (std::find(names.begin(), names.end(), want) != names.end())
			use = { want };
		else
		{
			logf("brain: char %d: no profile '%s', using merged", cid, want.c_str());
			use = names, merged = true;
		}
		ghost::Profile prof;
		for (const auto &n : use)
		{
			ghost::Profile other;
			if (!ghost::load_profile(dir + std::wstring(n.begin(), n.end()) + L".t7p", other))
				continue;
			if (prof.char_id < 0)
				prof = std::move(other);
			else
				ghost::absorb_profile(prof, std::move(other), merged);
		}
		if (prof.char_id < 0)
			continue;
		prof.merged = merged && use.size() > 1;
		std::string extra;
		for (const auto &n : prof.pool_names)
			extra += " +" + n;
		logf("brain: char %d -> %s %s%s (%zu chunks, %zu pooled, guard rate %.2f, throw breaks %d/%d -> %d%%)", cid, merged ? "merged" : use.size() > 1 ? "hybrid" : "profile",
			prof.name.c_str(), extra.c_str(), prof.chunks.size(), prof.pool.size(), prof.guard_rate,
			prof.throw_breaks, prof.throw_grabs, prof.throw_break_pct());
		b->profiles.emplace(cid, std::move(prof));
	}
	b->profile_names = by_char;
	// moves the AI has blocked before: frames it really had afterwards (learned live, kept across sessions)
	{
		std::ifstream lf(g_dir + L"learned_framedata.txt");
		std::string tag;
		int ch, mv, frames;
		size_t n = 0;
		while (lf >> tag >> ch >> mv >> frames)
			if (tag == "l")
				b->learned[ghost::move_key(ch, mv)].push_back(frames), ++n;
		logf("brain: %zu learned block readings for %zu moves", n, b->learned.size());
	}
	// step 5: the human's habit model, one file per human character, kept across sessions
	b->on_model_needed = [](ghost::Brain &br, int human_char) -> ghost::OpponentModel * {
		if (human_char < 0 || human_char > 80)
			return nullptr;
		auto it = br.models.find(human_char);
		if (it == br.models.end())
		{
			ghost::OpponentModel m;
			m.char_id = human_char;
			const bool had = m.load(g_dir + L"opponent_" + std::to_wstring(human_char) + L".txt");
			logf("adapt: %s habit model for your char %d", had ? "loaded" : "new", human_char);
			it = br.models.emplace(human_char, std::move(m)).first;
		}
		return &it->second;
	};
	b->on_model_changed = [](const ghost::OpponentModel &m) {
		m.save(g_dir + L"opponent_" + std::to_wstring(m.char_id) + L".txt");
	};
	b->on_learn = [](uint32_t key, int frames) {
		if (FILE *f = _wfsopen((g_dir + L"learned_framedata.txt").c_str(), L"ab", _SH_DENYWR))
		{
			fprintf(f, "l %u %u %d\n", key >> 16, key & 0xFFFF, frames);
			fclose(f);
		}
		logf("ai: learned: blocked char %u move %u -> %d frames to act", key >> 16, key & 0xFFFF, frames);
	};
	if (b->wallmap.load(g_dir + L"walls.txt"))
		logf("brain: wall maps for %zu stages (wall awareness %s)", b->wallmap.stages.size(), b->cfg.wall_awareness ? "on" : "off");
	else
		logf("brain: no walls.txt (wall awareness has no maps)");
	{
		int n = 0;
		WIN32_FIND_DATAW fd;
		if (HANDLE h = FindFirstFileW((g_dir + L"combos\\routes_*.txt").c_str(), &fd); h != INVALID_HANDLE_VALUE)
		{
			do
				n += ghost::load_routes(g_dir + L"combos\\" + fd.cFileName, b->routes);
			while (FindNextFileW(h, &fd));
			FindClose(h);
		}
		logf("brain: %d combo routes for %zu characters (combo style %d)", n, b->routes.size(), b->cfg.combo_style);
		n = 0;
		if (HANDLE h = FindFirstFileW((g_dir + L"combos\\punishes_*.txt").c_str(), &fd); h != INVALID_HANDLE_VALUE)
		{
			do
				if (!wcsstr(fd.cFileName, L"_candidates"))
					n += ghost::load_punishes(g_dir + L"combos\\" + fd.cFileName, b->punishes);
			while (FindNextFileW(h, &fd));
			FindClose(h);
		}
		logf("brain: %d punishes for %zu characters (punishment proficiency %d%%)", n, b->punishes.size(), b->cfg.punish_skill);
		n = 0;
		if (HANDLE h = FindFirstFileW((g_dir + L"combos\\taunts_*.txt").c_str(), &fd); h != INVALID_HANDLE_VALUE)
		{
			do
				n += ghost::load_taunts(g_dir + L"combos\\" + fd.cFileName, b->taunts);
			while (FindNextFileW(h, &fd));
			FindClose(h);
		}
		logf("brain: %d taunts for %zu characters (taunts %s)", n, b->taunts.size(), b->cfg.taunts ? "on" : "off");
	}
	if (ghost::load_framedata(g_dir + L"framedata.txt", b->framedata))
		logf("brain: frame data for %zu moves", b->framedata.size());
	else
		logf("brain: no framedata.txt (punishing uses live recovery only)");
	logf("brain: enabled=%d reaction=%d read_chance=%d%% punish=%d combo_style=%d guard_skill=%d decision_skill=%d adapt=%d throw_break=%d, %zu characters",
		b->cfg.enabled, b->cfg.reaction_frames, b->cfg.input_read_chance, b->cfg.punish, b->cfg.combo_style, b->cfg.guard_skill, b->cfg.decision_skill, b->cfg.adapt, b->cfg.throw_break, b->profiles.size());
	return b;
}

// AI trace: one line per frame while the AI drives P2 (t7ghost/ai_trace_<time>.log).
// Columns: frame | sent held/pressed | decoded dir (+0xDDC) | decision | situation | state
FILE *g_trace = nullptr;
uint64_t g_trace_n = 0;

void ai_trace(ghost::Brain *b, uintptr_t p, uint32_t held, uint32_t pressed, int side)
{
	if (!g_trace)
	{
		g_trace = _wfsopen(stamp_name(L"ai_trace_", L".log").c_str(), L"wb", _SH_DENYWR);
		g_trace_n = 0;
		if (!g_trace)
			return;
		fputs("# n held pressed -> dir | ev | sit seen_move/atk | my_move cls | opp_move/atk | dist hp opp_hp rage opp_rage | left side adv\n", g_trace);
	}
	int32_t dir = 0;
	t7::rd(p + 0xDDC, dir);
	const auto &d = b->dbg;
	static const char *sits[] = { "neutral", "opp_startup", "opp_recovery", "opp_stunned", "opp_juggled", "opp_down", "self_down" };
	static const char *cl[] = { "free", "attack", "stun", "down", "juggled" };
	fprintf(g_trace, "%llu %X %X -> %d | %s | %s %d/%d | %d %s | %d/%d | %.0f %d %d %d %d | %zu %d %d\n", g_trace_n++, held, pressed, dir,
		d.decided ? b->last_event.c_str() : "-", sits[d.sit % 7], d.seen_move, d.seen_atk, d.my_move, cl[d.cls % 5], d.opp_move,
		d.opp_atk, d.dist, d.hp, d.opp_hp, d.rage, d.opp_rage, d.left, side, d.adv);
	if ((g_trace_n & 63) == 0)
		fflush(g_trace);
}

// Which side the AI drives (0 = P1, 1 = P2, -1 = none). Offline only (game mode 2):
//   Practice: the dummy (P2) when it is set to CPU at Ultra Hard;
//   Arcade / Treasure Battle (if enabled): whichever side the game's CPU controls (+0x1570), either side;
//   VS (two humans), online, replays, menus: never.
const char *g_ai_mode = "";
int ai_side(const ghost::Settings &cfg)
{
	if (t7::read_global(t7::kGameMode) != t7::kModePractice) // mode 2 = every offline battle
		return -1;
	const int bt = t7::read_global(t7::kBattleType);
	if (bt == t7::kBattlePractice)
	{
		g_ai_mode = "Practice";
		return t7::read_global(t7::kDummyIsCpu) == 1 && t7::read_global(t7::kCpuLevel) == t7::kUltraHard ? 1 : -1;
	}
	if ((bt == t7::kBattleArcade && cfg.arcade) || (bt == t7::kBattleTreasure && cfg.treasure))
	{
		g_ai_mode = bt == t7::kBattleArcade ? "Arcade" : "Treasure Battle";
		const uintptr_t base = t7::exe_base();
		int32_t c1 = 0, c2 = 0;
		if (!t7::rd(base + t7::kLiveP1 + t7::kIsCpu, c1) || !t7::rd(base + t7::kLiveP2 + t7::kIsCpu, c2))
			return -1;
		if (c1 == 1 && c2 != 1)
			return 0;
		if (c2 == 1 && c1 != 1)
			return 1;
	}
	return -1;
}

// Game Modifiers: offline battles only (game mode 2: Practice, VS, Arcade, Treasure / Ghost Battle - never
// online or replays). The settings are latched when a battle starts, so a change applies from the next match.
// Runs on the game thread, once per player per frame (decode_hook).
struct Mods { bool no_rage = false, no_pc = false, no_screw = false; int health = 100; } g_mods;

// Health multiplier (2026-10-25, a write watch on the health percent +0x1508: exe+0x742C70 computes it every
// frame as current * 100 / max). The health object is at player +0x14E0: max health at +0x14FC (16.16 fixed,
// 175.0; +0x14F8 and +0x1500 hold the same), current health ENCODED - check dword +0x14E8 with key qword
// +0x14F0, value = 0x255A60(check, key) (0 if the check fails = a KO), new check = 0x2D0EA0(value, key).
// We scale the max copies and keep the current health in step: whenever the game (re)sets the max (a new
// match / round) and whenever the health goes UP (round start, Practice refill), it becomes the scaled max.
constexpr uintptr_t kHealth = 0x14E0, kFnDecodeInt = 0x255A60, kFnEncodeInt = 0x2D0EA0;
struct HealthState { int32_t base = 0, target = 0, last = 0; } g_health[2];
int32_t health_get(uintptr_t p)
{
	uint32_t chk = 0;
	uint64_t key = 0;
	if (!t7::rd(p + kHealth + 8, chk) || !t7::rd(p + kHealth + 0x10, key))
		return 0;
	return reinterpret_cast<int32_t(__fastcall *)(uint32_t, uint64_t)>(t7::exe_base() + kFnDecodeInt)(chk, key);
}
void health_set(uintptr_t p, int32_t v)
{
	uint64_t key = 0;
	if (!t7::rd(p + kHealth + 0x10, key))
		return;
	const uint32_t chk = reinterpret_cast<uint32_t(__fastcall *)(int32_t, uint64_t)>(t7::exe_base() + kFnEncodeInt)(v, key);
	*reinterpret_cast<volatile uint32_t *>(p + kHealth + 8) = chk;
}
void health_tick(uintptr_t p, int idx, int pct)
{
	HealthState &st = g_health[idx];
	int32_t mx = 0;
	if (!t7::rd(p + 0x14FC, mx) || mx <= 0)
		return;
	if (pct == 100 && (st.target == 0 || mx == st.base))
	{
		st = {}; // off (and nothing of ours left to undo)
		return;
	}
	const int32_t cur = health_get(p);
	if (mx != st.target)
	{
		// the game set the max (new match / round, or our first frame): scale it
		st.base = mx, st.target = int32_t(int64_t(mx) * pct / 100);
		for (const uintptr_t o : { 0x14F8, 0x14FC, 0x1500 })
			*reinterpret_cast<volatile int32_t *>(p + o) = st.target;
		if (cur >= st.base)
			health_set(p, st.target);
		logf("mods: health P%d: max %d -> %d (x%d%%), current %d -> %d", idx + 1, st.base >> 16, st.target >> 16, pct, cur >> 16,
			health_get(p) >> 16);
		st.last = health_get(p);
		if (pct == 100)
			st = {};
		return;
	}
	if (cur > st.last && cur < st.target)
	{
		// health went up (a new round, a Practice refill): to the scaled max
		health_set(p, st.target);
		logf("mods: health P%d: refill %d -> %d", idx + 1, cur >> 16, st.target >> 16);
	}
	st.last = health_get(p);
}
std::atomic<bool> g_mods_relatch{ false }; // a new match without leaving the mode (Ghost Battle's next fight)
// No Power Crush: the two power crush property cases (exe+0x71D10F / 0x71D15A: mov byte [rcx+0x6C0], al) don't
// set the armor flag at all while the modifier is on - patched only in offline battles (game mode 2), restored the
// first frame of anything else (online included). Runs on the game thread (mods_tick), the thread that runs it.
void pc_patch(bool on)
{
	// the two power crush cases of the move property switch (variant 1 at 0x71D0F4, variant 2 at 0x71D14C): their
	// armor flag writes out. (Their +0x6C1 / +0x6C8 flags were not the smoke - that is property 0x8046, skipped
	// in gb::propexec_hook.)
	struct Patch { uintptr_t at; uint8_t orig[6], repl[6]; size_t n; };
	static const Patch patches[] = {
		{ 0x71D10F, { 0x88, 0x81, 0xC0, 0x06, 0x00, 0x00 }, { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 }, 6 }, // mov [rcx+0x6C0], al
		{ 0x71D15A, { 0x88, 0x81, 0xC0, 0x06, 0x00, 0x00 }, { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 }, 6 }, // mov [rcx+0x6C0], al
	};
	static bool patched = false, failed = false;
	if (on == patched || failed)
		return;
	for (const Patch &pt : patches)
		if (std::memcmp(reinterpret_cast<void *>(t7::exe_base() + pt.at), patched ? pt.repl : pt.orig, pt.n) != 0)
		{
			failed = true;
			logf("mods: no power crush: unexpected code at exe+0x%llX, effect patch off", (unsigned long long)pt.at);
			return;
		}
	for (const Patch &pt : patches)
	{
		void *code = reinterpret_cast<void *>(t7::exe_base() + pt.at);
		DWORD old = 0;
		if (VirtualProtect(code, pt.n, PAGE_EXECUTE_READWRITE, &old))
		{
			std::memcpy(code, on ? pt.repl : pt.orig, pt.n);
			VirtualProtect(code, pt.n, old, &old);
		}
	}
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(t7::exe_base() + 0x71D100), 0x80);
	patched = on;
	logf("mods: no power crush: armor flag code %s", on ? "patched" : "restored");
}

void mods_tick(uintptr_t p, int idx, const ghost::Brain *b)
{
	static int prev_mode = -1;
	static uint64_t last_call = 0;
	const int mode = t7::read_global(t7::kGameMode);
	bool new_match = false;
	if (idx == 0)
	{
		// a new match: the mode starts, Ghost Battle's next fight, or the fight resumes after a loading screen
		// (no input processing for > 1.5 s - rematches and Character Select stay in mode 2) at full health
		const uint64_t now = GetTickCount64();
		int32_t h1 = 0, h2 = 0;
		const uintptr_t base = t7::exe_base();
		const bool fresh = now - last_call > 1500 && t7::rd(base + t7::kLiveP1 + 0x1508, h1) &&
			t7::rd(base + t7::kLiveP2 + 0x1508, h2) && h1 >= 100 && h2 >= 100;
		last_call = now;
		new_match = mode != prev_mode || g_mods_relatch.exchange(false) || fresh;
		prev_mode = mode;
	}
	if (new_match && mode == t7::kModePractice && b)
	{
		const Mods was = g_mods;
		g_mods.no_rage = b->cfg.mod_no_rage;
		g_mods.health = b->cfg.mod_health;
		g_mods.no_pc = b->cfg.mod_no_pc;
		g_mods.no_screw = b->cfg.mod_no_screw;
		if (was.no_screw && !g_mods.no_screw)
			for (const uintptr_t pl : { t7::exe_base() + t7::kLiveP1, t7::exe_base() + t7::kLiveP2 })
				*reinterpret_cast<volatile int32_t *>(pl + 0xB08) = 0; // screws back: clear our "screw used" mark
		if (was.no_rage && !g_mods.no_rage)
			for (const uintptr_t pl : { t7::exe_base() + t7::kLiveP1, t7::exe_base() + t7::kLiveP2 })
			{
				uint8_t f = 0; // give rage back: clear our "rage used" mark
				if (t7::rd(pl + 0x757, f) && (f & 4))
					*reinterpret_cast<volatile uint8_t *>(pl + 0x757) = uint8_t(f & ~4);
			}
		logf("mods: new match: no rage %s, health %d%%, no power crush %s, no screw %s", g_mods.no_rage ? "on" : "off", g_mods.health,
			g_mods.no_pc ? "on" : "off", g_mods.no_screw ? "on" : "off");
	}
	pc_patch(mode == t7::kModePractice && g_mods.no_pc);
#ifndef T7_PUBLIC
	// No Screw recon (2026-10-25): costume_watch_char=800 - record P2's whole player block (0x3670) every frame
	// with P1's move id, for 2 minutes (frames_p2.bin: per frame u32 n, i32 p1 move, i32 p2 move, block)
	if (idx == 1 && b && b->cfg.gb_costume_watch_char == 800 && mode == t7::kModePractice)
	{
		static FILE *rec = nullptr;
		static uint32_t n = 0;
		if (!rec && n == 0)
		{
			_wfopen_s(&rec, (g_dir + L"frames_p2.bin").c_str(), L"wb");
			logf("screw recon: recording P2 frames");
		}
		if (rec)
		{
			static uint8_t blk[0x3670];
			int32_t m1 = -1, m2 = -1;
			t7::rd(t7::exe_base() + t7::kLiveP1 + 0x350, m1), t7::rd(p + 0x350, m2);
			if (t7::safe_copy(blk, p, sizeof blk))
			{
				fwrite(&n, 4, 1, rec), fwrite(&m1, 4, 1, rec), fwrite(&m2, 4, 1, rec), fwrite(blk, 1, sizeof blk, rec);
				++n;
			}
			if (n >= 60 * 120)
				fclose(rec), rec = nullptr, logf("screw recon: %u frames recorded", n);
		}
	}
#endif
	if (mode != t7::kModePractice)
		return;
	health_tick(p, idx, g_mods.health);
	if (g_mods.no_screw)
	{
		// screw (2026-10-25, every-frame recording of P2 through screw / no-screw combos): +0xB08 = -1 from the screw hit until
		// the victim gets up (0 again at the wake-up reaction 32774); a second screw in the same combo gives a
		// normal juggle reaction. Keep it at -1: every screw hit acts like a combo's second one.
		int32_t used = 0;
		if (t7::rd(p + 0xB08, used) && used == 0)
			*reinterpret_cast<volatile int32_t *>(p + 0xB08) = -1;
	}
	if (g_mods.no_pc)
	{
		// power crush (2026-10-25): the move property handler (exe+0x71D0F0..) sets +0x6C0 = the armor flag (byte)
		// and +0x6C4 = the damage percent taken while armored (100; exe+0x7506BA: damage * it / 100, min 1 - zeroing
		// it made armored hits do 1 damage, author's test). Clear the flag, keep the damage.
		static int32_t logged_move[2] = { -1, -1 };
		uint8_t armor = 0;
		int32_t mv = 0;
		if (t7::rd(p + 0x6C0, armor) && armor != 0)
		{
			*reinterpret_cast<volatile uint8_t *>(p + 0x6C0) = 0;
			if (t7::rd(p + 0x350, mv) && mv != logged_move[idx])
				logged_move[idx] = mv, logf("mods: no power crush: P%d move %d armor flag cleared", idx + 1, mv);
		}
	}
	if (g_mods.no_rage)
	{
		// rage (2026-10-19, VS dumps in / out of / after rage): +0x781 = in rage (the game copies it in from
		// its state each frame), +0xC00 = the aura (exe+0x722C60 turns it on when +0x781 becomes 1). Clearing
		// them made the game start rage again every frame (endless activation effect). Instead: +0x757
		// bit 0x4 is only set once rage has been used ("rage spent" dump: 9 -> 13) - set it from the start.
		uint8_t f = 0;
		if (t7::rd(p + 0x757, f) && !(f & 4))
			*reinterpret_cast<volatile uint8_t *>(p + 0x757) = uint8_t(f | 4);
	}
}

std::atomic<uint32_t> g_decode_calls[3] = {}; // recon: decode_input calls for P1 / P2 / other objects
std::atomic<uint32_t> g_decode_input[2] = {};  // recon: frames with any held input, per side

void __fastcall decode_hook(uintptr_t p, int side, uint32_t held, uint32_t pressed)
{
	const uintptr_t base = t7::exe_base();
	const int idx = (p == base + t7::kLiveP2) ? 1 : (p == base + t7::kLiveP1) ? 0 : -1;
	++g_decode_calls[idx < 0 ? 2 : idx];
	if (idx >= 0 && held)
		++g_decode_input[idx];
	ghost::Brain *b = idx >= 0 ? g_brain.load() : nullptr;
	if (idx >= 0)
		mods_tick(p, idx, b);
	const int ai = b ? ai_side(b->cfg) : -1;
	if (idx >= 0 && ai < 0 && g_ai_active.exchange(false))
	{
		// left the mode (menus, VS, online...): hand the controls back
		g_ai_idx = -1;
		if (g_trace)
		{
			fclose(g_trace);
			g_trace = nullptr;
		}
		logf("ai: off (left %s)", g_ai_mode);
	}
	if (idx == 1 && g_mirror && t7::read_global(t7::kGameMode) == t7::kModePractice)
		held = g_raw[0][0], pressed = g_raw[0][1];
	else if (idx >= 0 && idx == ai)
	{
		int32_t ch = -1;
		t7::rd(p + 0xDC, ch);
		const bool on = b->cfg.enabled && b->has_profile(ch);
		static int on_side = -1, on_char = -1;
		// a new fight against another character (Arcade's next opponent) starts fresh as well
		if (on != g_ai_active || (on && (idx != on_side || ch != on_char)))
		{
			b->reset();
			g_ai_active = on;
			on_side = on ? idx : -1;
			on_char = on ? ch : -1;
			g_ai_idx = on_side;
			if (!on && g_trace)
			{
				fclose(g_trace);
				g_trace = nullptr;
			}
			logf("ai: %s (%s, P%d char %d, profile %s)", on ? "ON" : "off", g_ai_mode, idx + 1, ch,
				on ? b->profile_label(ch).c_str() : "-");
		}
		static int last_side = -1;
		if (on && side != last_side)
		{
			last_side = side;
			logf("ai: decode side arg = %d", side); // relative inputs verified only for side 0
		}
		uint32_t h, pr;
		const uintptr_t blk = t7::newest_block();
		const uintptr_t opp = base + (idx ? t7::kLiveP1 : t7::kLiveP2);
		const uintptr_t ring_me = blk ? blk + (idx ? t7::kP2 : 0) : 0, ring_opp = blk ? blk + (idx ? 0 : t7::kP2) : 0;
		if (on && b->step(p, opp, ring_me, ring_opp, ch, h, pr))
		{
			held = h, pressed = pr;
			g_decode_orig(p, side, held, pressed);
#ifndef T7_PUBLIC
			ai_trace(b, p, held, pressed, side); // per-frame dev trace (megabytes per session)
#endif
			if (const std::string note = b->take_adapt_note(); !note.empty())
				logf("adapt: you%s", note.c_str()); // what the AI has noticed about your habits so far
			goto decoded;
		}
	}

	g_decode_orig(p, side, held, pressed);
decoded:

	if (idx < 0)
		return;
	g_raw[idx][0] = held;
	g_raw[idx][1] = pressed;
	g_side[idx] = side;
}

#include "ghostbattle.hpp"
#include "gbmenu.hpp"
#include "gbtext.hpp"
#ifndef T7_PUBLIC
#include "menutrace.hpp"
#endif
#include "overlay.hpp"

bool install_hooks()
{
	if (MH_Initialize() != MH_OK)
		return false;
	auto target = reinterpret_cast<void *>(t7::exe_base() + kDecodeInput);
	// sanity: expected prologue "mov [rsp+8],rbx; mov [rsp+18h],r8d"
	static const uint8_t prologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x44, 0x89, 0x44, 0x24, 0x18 };
	if (std::memcmp(target, prologue, sizeof prologue) != 0)
	{
		logf("hook: decode_input prologue mismatch, not hooking");
		return false;
	}
	if (MH_CreateHook(target, reinterpret_cast<void *>(&decode_hook), reinterpret_cast<void **>(&g_decode_orig)) != MH_OK ||
		MH_EnableHook(target) != MH_OK)
	{
		logf("hook: MinHook failed");
		return false;
	}
	logf("hook: decode_input hooked");
	gb::install();
	gbm::install();
#ifndef T7_PUBLIC
	mtrace::install();
#endif
	return true;
}

// ---------------------------------------------------------------- status

void write_status()
{
	uint32_t fc = 0;
	const uintptr_t blk = t7::newest_block(&fc);
	FILE *f;
	if (_wfopen_s(&f, (g_dir + L"status.txt").c_str(), L"wb") != 0)
		return;
	fprintf(f, "ring=%p newest=%p frame=%u rec=%s watch=%s trace=%d\n", (void *)t7::ring_base(), (void *)blk, fc,
		g_rec.f ? "on" : "off", g_watching ? "on" : "off", g_tracer.f ? 1 : 0);
	if (blk)
	{
		for (const auto &fd : t7::kFields)
		{
			const double a = t7::read_field(blk, fd), b = t7::read_field(blk + t7::kP2, fd);
			if (fd.ty == t7::Ty::f32)
				fprintf(f, "%-14s %12.3f %12.3f\n", fd.name, a, b);
			else
				fprintf(f, "%-14s %12.0f %12.0f   (0x%X / 0x%X)\n", fd.name, a, b, (uint32_t)(int64_t)a, (uint32_t)(int64_t)b);
		}
	}
	fclose(f);
}

// Wall survey: in Practice, both players' root positions (+0x1610..) and state are appended to
// t7ghost/walls_survey.txt with the stage id, whenever someone moved >= 40 units, so normal
// practice play (and walking along the walls on purpose) builds the per-stage wall maps.
// Line: <stage> <player 0/1> <px> <py> <pz> <cls hint: simple_state> <stun>
// ---------------------------------------------------------------- game-mode recon (Arcade / Treasure Battle)
// Logs every game-mode change with the practice-CPU globals, stage and characters, and Ctrl+F5 dumps
// both live player objects (and the .data globals near the known mode/CPU fields) to
// t7ghost/dump_<time>_p1.bin / _p2.bin / _globals.bin, to find which side the CPU controls.
void mode_tick()
{
	static int last = -12345;
	static ULONGLONG next_report = 0;
	const int mode = t7::read_global(t7::kGameMode);
	if (GetTickCount64() >= next_report)
	{
		next_report = GetTickCount64() + 10000;
		const uint32_t a = g_decode_calls[0].exchange(0), b = g_decode_calls[1].exchange(0), o = g_decode_calls[2].exchange(0);
		const uint32_t ia = g_decode_input[0].exchange(0), ib = g_decode_input[1].exchange(0);
		if (mode != 0 && (a || b))
		{
			// candidate "this side is the CPU" fields (Arcade/Treasure recon 2026-10-18: +0x1570 human 0 /
			// cpu 1, +0x0008 human 257 / cpu 1, +0x0338 human 16 / cpu 0)
			int32_t f[2][3] = {};
			const uintptr_t base = t7::exe_base(), obj[2] = { base + t7::kLiveP1, base + t7::kLiveP2 };
			for (int i = 0; i < 2; ++i)
				t7::rd(obj[i] + 0x1570, f[i][0]), t7::rd(obj[i] + 0x8, f[i][1]), t7::rd(obj[i] + 0x338, f[i][2]);
			logf("mode: %d - decode_input calls in 10 s: P1 %u (input on %u), P2 %u (input on %u), other %u | "
				"cpu? +1570 %d/%d +0008 %d/%d +0338 %d/%d | dummy_is_cpu %d cpu_level %d", mode, a, ia, b, ib, o,
				f[0][0], f[1][0], f[0][1], f[1][1], f[0][2], f[1][2], t7::read_global(t7::kDummyIsCpu), t7::read_global(t7::kCpuLevel));
		}
	}
	if (mode == last)
		return;
	last = mode;
	int32_t c1 = -1, c2 = -1;
	t7::rd(t7::exe_base() + t7::kLiveP1 + 0xDC, c1);
	t7::rd(t7::exe_base() + t7::kLiveP2 + 0xDC, c2);
	logf("mode: game mode %d (dummy_is_cpu %d, cpu_level %d, stage %d, chars %d vs %d)", mode,
		t7::read_global(t7::kDummyIsCpu), t7::read_global(t7::kCpuLevel), t7::read_global(t7::kStageId), c1, c2);
}

// Character Select recon (dev, 2026-10-19): watch who reads a Treasure dialog's result - chosen index
// (+0xE4) and decided flag (+0xEA, in the dword at +0xE8) - to find the owners (result screen, pause menu)
// and the code behind the pause menu's "Return to Character Select". Armed when a new Treasure dialog shows
// up, dumped when it changes or after 20 s.
void dialog_watch_tick()
{
	static uintptr_t armed = 0;
	static ULONGLONG since = 0;
	const uintptr_t d = gbm::g_dialog;
	if (armed && (d != armed || GetTickCount64() - since > 20000))
	{
		set_debug_regs(0, 0, false);
		dump_hits("dialog result readers");
		armed = 0;
	}
	if (d && d != armed && !g_watching && t7::read_global(t7::kBattleType) == t7::kBattleTreasure)
	{
		if (!g_veh)
			g_veh = AddVectoredExceptionHandler(1, veh);
		{
			std::lock_guard lk(g_hits_mx);
			g_hits.clear();
		}
		logf("watch: dialog %p, result fields +0xE4 / +0xE8", (void *)d);
		set_debug_regs(d + 0xE4, d + 0xE8, true, true);
		armed = d, since = GetTickCount64();
	}
}

void dump_players()
{
	const uintptr_t base = t7::exe_base();
	const std::wstring stem = stamp_name(L"dump_", L"");
	auto dump = [&](const wchar_t *suffix, uintptr_t addr, size_t n) {
		std::vector<uint8_t> buf(n);
		if (!t7::safe_copy(buf.data(), addr, n))
			return false;
		FILE *f = _wfsopen((stem + suffix).c_str(), L"wb", _SH_DENYWR);
		if (!f)
			return false;
		fwrite(buf.data(), 1, n, f);
		fclose(f);
		return true;
	};
	const bool a = dump(L"_p1.bin", base + t7::kLiveP1, 0x3670), b = dump(L"_p2.bin", base + t7::kLiveP2, 0x3670);
	const bool g = dump(L"_globals.bin", base + 0x3447000, 0x2000); // around the practice CPU globals
	logf("dump: players %s/%s, globals %s (mode %d, stage %d)", a ? "ok" : "FAIL", b ? "ok" : "FAIL", g ? "ok" : "FAIL",
		t7::read_global(t7::kGameMode), t7::read_global(t7::kStageId));
	// costume recon (2026-10-19, preset costumes): the battle settings with both sides' costume blocks
	// (+0xB8 + side*0x460) and both player-info entries
	const uintptr_t cfg = gb::battle_cfg(), i0 = gb::player_info(0), i1 = gb::player_info(1);
	const bool cf = cfg && dump(L"_cfg.bin", cfg, 0x1000);
	const bool in = i0 && i1 && dump(L"_info0.bin", i0, 0x400) && dump(L"_info1.bin", i1, 0x400);
	if (cfg)
		for (int s = 0; s < 2; ++s)
		{
			uint32_t w[4] = {};
			t7::safe_copy(w, cfg + gb::kCfgCostume + s * gb::kCostumeStride, sizeof w);
			int32_t ch = -1;
			t7::rd(cfg + gb::kCfgChar + s * 4, ch);
			logf("dump: side %d char %d costume block %08X %08X %08X %08X", s, ch, w[0], w[1], w[2], w[3]);
		}
	logf("dump: cfg %s, infos %s", cf ? "ok" : "FAIL", in ? "ok" : "FAIL");
	// own-costume recon (2026-10-25): a fighter's outfit = 0x1AC bytes at costume block +0x1B4 (copied there by
	// 0x292E30, first dword = the slot). With the player in their own custom costume, search the process for
	// other copies of P1's block (the save data) and the exe globals that point into those allocations.
	if (cfg)
		for (int s = 0; s < 2; ++s)
		{
			std::vector<uint8_t> blk(0x1AC);
			const uintptr_t at = cfg + gb::kCfgCostume + s * gb::kCostumeStride + 0x1B4;
			if (!t7::safe_copy(blk.data(), at, blk.size()))
				continue;
			wchar_t sfx[32];
			swprintf(sfx, 32, L"_outfit%d.bin", s);
			dump(sfx, at, blk.size());
			// the pattern: the block after its slot dword, if it holds anything. 0x60 bytes from +4 matched every
			// design of a character alike (shared base pieces, 2026-10-25) - now 0x140 bytes from +0x60, one design only
			const uint8_t *pat = blk.data() + 0x60;
			const size_t plen = 0x140;
			if (std::all_of(pat, pat + plen, [](uint8_t v) { return v == 0; }))
			{
				logf("dump: outfit side %d empty", s);
				continue;
			}
			struct Hit { uintptr_t addr, base, size; };
			std::vector<Hit> hits;
			MEMORY_BASIC_INFORMATION mbi{};
			std::vector<uint8_t> buf;
			for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && hits.size() < 24 && VirtualQuery(reinterpret_cast<void *>(a), &mbi, sizeof mbi);
				 a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize)
			{
				if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
					!(mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) || mbi.RegionSize > 0x40000000)
					continue;
				const uintptr_t rb = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
				buf.resize(mbi.RegionSize);
				if (!t7::safe_copy(buf.data(), rb, buf.size()))
					continue;
				for (auto it = buf.begin(); hits.size() < 24;)
				{
					it = std::search(it, buf.end(), pat, pat + plen);
					if (it == buf.end())
						break;
					const uintptr_t h = rb + (it - buf.begin()) - 0x60; // the block's start (slot dword 0x60 before the pattern)
					if (h != at)
						hits.push_back({ h, reinterpret_cast<uintptr_t>(mbi.AllocationBase), mbi.RegionSize });
					++it;
				}
			}
			int32_t ch = -1, slot = -1;
			t7::rd(cfg + gb::kCfgChar + s * 4, ch), t7::rd(at, slot);
			logf("dump: outfit side %d (char %d, slot %d): %zu other copies", s, ch, slot, hits.size());
			const uintptr_t ebase = t7::exe_base();
			const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(ebase);
			const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(ebase + dos->e_lfanew);
			const size_t esize = nt->OptionalHeader.SizeOfImage;
			int k = 0;
			for (const Hit &h : hits)
			{
				uint32_t slot_here = 0;
				t7::rd(h.addr, slot_here);
				logf("dump:   copy %p (slot dword %u), allocation %p, region size 0x%zX", (void *)h.addr, slot_here, (void *)h.base, h.size);
				if (k < 4)
				{
					wchar_t nsfx[48];
					swprintf(nsfx, 48, L"_outfit%d_copy%d.bin", s, k);
					const uintptr_t from = h.addr >= 0x4000 ? h.addr - 0x4000 : h.addr;
					dump(nsfx, from, 0x8000); // 0x4000 before the copy, 0x4000 from it
					// exe globals (8-byte values) pointing into this allocation
					int g = 0;
					for (uintptr_t e = ebase; e + 8 <= ebase + esize && g < 12; e += 8)
					{
						uintptr_t v = 0;
						if (!t7::rd(e, v) || v < h.base || v >= h.addr + 0x10000)
							continue;
						logf("dump:     global exe+0x%llX -> %p (copy - 0x%llX)", (unsigned long long)(e - ebase), (void *)v,
							(unsigned long long)(h.addr - v));
						++g;
					}
				}
				++k;
			}
		}
	// list-file recon (preset costumes): globals near exe+0x34CE8A0 that are "list" objects - +0x28/+0x30
	// = vector of loaded files, data = [[file]+0x48] (0x329170). Dump each file's data (header: +8 u16 count).
	int lists = 0;
	for (uintptr_t g = 0x34CE000; g < 0x34D0000; g += 8)
	{
		uintptr_t obj = 0, b0 = 0, b1 = 0;
		if (!t7::rd(base + g, obj) || obj < 0x10000 || !t7::rd(obj + 0x28, b0) || !t7::rd(obj + 0x30, b1) ||
			!b0 || b1 < b0 || b1 - b0 > 64 * 8 || (b1 - b0) % 8)
			continue;
		for (uintptr_t k = 0; k * 8 < b1 - b0; ++k)
		{
			uintptr_t file = 0, vt = 0, data = 0;
			if (!t7::rd(b0 + k * 8, file) || !file || !t7::rd(file, vt) || !vt || !t7::rd(vt + 0x48, data) || !data)
				continue;
			uint16_t count = 0;
			t7::rd(data + 8, count);
			wchar_t sfx[64];
			swprintf(sfx, 64, L"_list_%X_%u.bin", unsigned(g), unsigned(k));
			size_t n = 0x40000;
			while (n >= 0x400 && !dump(sfx, data, n))
				n /= 2;
			logf("dump: list global +0x%X file %u data count %u (%zu bytes saved)", unsigned(g), unsigned(k), count, n >= 0x400 ? n : 0);
			++lists;
		}
	}
	logf("dump: %d list files", lists);
}

void survey_tick()
{
	static FILE *f = nullptr;
	static float last[2][3] = {};
	if (t7::read_global(t7::kGameMode) != 2)
		return;
	if (!f && !(f = _wfsopen((g_dir + L"walls_survey.txt").c_str(), L"ab", _SH_DENYWR)))
		return;
	const int stage = t7::read_global(t7::kStageId);
	const uintptr_t base = t7::exe_base();
	const uintptr_t obj[2] = { base + t7::kLiveP1, base + t7::kLiveP2 };
	static ULONGLONG next_flush = 0;
	for (int i = 0; i < 2; ++i)
	{
		float p[3];
		int32_t simple = 0, stun = 0;
		if (!t7::rd(obj[i] + 0x1610, p[0]) || !t7::rd(obj[i] + 0x1614, p[1]) || !t7::rd(obj[i] + 0x1618, p[2]) ||
			!t7::rd(obj[i] + 0x428, simple) || !t7::rd(obj[i] + 0x42C, stun))
			continue;
		const float dx = p[0] - last[i][0], dz = p[2] - last[i][2], dy = p[1] - last[i][1];
		if (dx * dx + dz * dz + dy * dy < 40.0f * 40.0f)
			continue;
		memcpy(last[i], p, sizeof p);
		fprintf(f, "%d %d %.0f %.0f %.0f %d %d\n", stage, i, p[0], p[1], p[2], simple, stun);
	}
	if (GetTickCount64() >= next_flush)
		fflush(f), next_flush = GetTickCount64() + 2000;
}

DWORD WINAPI worker(void *)
{
	timeBeginPeriod(1);
	logf("t7ghost v%s up. exe=%p stamp=0x%08X", kModVersion, (void *)t7::exe_base(), t7::exe_stamp());
	if (t7::exe_stamp() != t7::kExeStamp)
	{
		logf("unsupported game build (expected 0x%08X); staying inert", t7::kExeStamp);
		warn_user(L"T7 Ghost AI is not active: this TEKKEN 7 version is not supported.\n\n"
			L"The mod needs the latest Steam version of TEKKEN 7. Update the game through Steam "
			L"(or right-click TEKKEN 7 > Properties > Installed Files > Verify integrity of game files).\n\n"
			L"The game itself runs normally; the mod stays switched off.");
		return 0;
	}
#ifdef T7_PUBLIC
	// public build: the AI and its learning only - no recorders, traces, surveys, status file or dev
	// hotkeys (and no name key: nothing about other players is read or written)
	g_brain = load_brain();
	install_hooks();
	overlay_install();
	for (;;)
	{
		if (hotkey(VK_F9))
			g_brain = load_brain(), load_menu_key(); // old brain intentionally leaked: the game thread may still hold it
		overlay_tick();
		gbt::tick(); // "GHOST BATTLE" in the menus
		Sleep(15);
	}
#endif
	if (!names::load_key(g_dir))
		logf("names: could not create/load key; name hashes disabled");
	g_brain = load_brain();
	install_hooks();
	overlay_install();
	ULONGLONG next_status = 0;
	for (;;)
	{
		if (hotkey(VK_F6)) g_rec.toggle();
		if (hotkey(VK_F12) || g_combo_toggle.exchange(false)) g_rec.toggle_combo();
		if (hotkey(VK_F5)) dump_players();
		if (hotkey(VK_F4)) gbt::in_background([] { gbt::report(); });
		if (hotkey(VK_F3)) gbt::in_background([] { gbt::rename(); });
		gbt::tick();
		mode_tick();
		if (hotkey(VK_F7)) g_tracer.toggle();
#ifndef T7_PUBLIC
		if (hotkey(VK_F8)) g_tracer.mark(), mtrace::mark();
		if (hotkey(VK_F2)) mtrace::toggle();
		// dialog_watch_tick(); // done (2026-10-19): readers found; the getter hooks in menutrace.hpp log the owners
#endif
		g_tracer.tick();
#ifndef T7_PUBLIC
		if (ghost::Brain *wb = g_brain)
			gb::slotwatch::costume_watch_tick(wb->cfg.gb_costume_watch_char);
		gb::slotwatch::tick();
#endif
		if (hotkey(VK_F10)) toggle_watch();
		if (hotkey(VK_F9))
			g_brain = load_brain(), load_menu_key(); // old brain intentionally leaked: the game thread may still hold it
		if (hotkey(VK_F11))
		{
			g_mirror = !g_mirror;
			logf("mirror test %s", g_mirror ? "ON" : "OFF");
		}
		overlay_tick();
		g_rec.auto_mode();
		survey_tick();
		watch_tick();
		g_rec.poll();
		if (g_rec.f && g_rec.automatic)
		{
			uint32_t fc = 0;
			if (t7::newest_block(&fc))
				g_finder.tick(fc);
		}
		if (GetTickCount64() >= next_status)
		{
			write_status();
			next_status = GetTickCount64() + 500;
		}
		Sleep(2);
	}
}

}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(mod);
		// installed as both dinput8.dll and version.dll: only the first copy starts; an old version.dll of
		// ours next to this dinput8.dll would run too - stay off and say so
		if (!loader::first_in_process())
			return TRUE;
		if (loader::old_version_dll_next_to(mod))
		{
			loader::message(loader::kOldVersionDll);
			return TRUE;
		}
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(mod, path, MAX_PATH);
		g_dir = path;
		g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/") + 1) + L"t7ghost\\";
		CreateDirectoryW(g_dir.c_str(), nullptr);
		g_log = _wfsopen((g_dir + L"t7ghost.log").c_str(), L"ab", _SH_DENYWR);
		CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr));
	}
	return TRUE;
}
