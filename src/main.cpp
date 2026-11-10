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

#define T7_VERSION_STR "1.2a" // keep in step with the t7ghost.ini header (tools/write_ini.py)
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
	b->cfg.mod_no_wall_bound = geti(L"modifiers", L"no_wall_bound", 0) != 0;
	b->cfg.mod_backdash = std::clamp(geti(L"modifiers", L"backdash", 100), 1, 200);
	b->cfg.mod_sidestep = std::clamp(geti(L"modifiers", L"sidestep", 100), 1, 200);
	b->cfg.mod_camera = std::clamp(geti(L"modifiers", L"camera", 100), 1, 200);
	b->cfg.mod_camera_height = std::clamp(geti(L"modifiers", L"camera_height", 0), -100, 100);
	b->cfg.mod_camera_height_free = geti(L"modifiers", L"camera_height_free", 0) != 0;
	b->cfg.mod_hud_x = std::clamp(geti(L"modifiers", L"hud_x", 0), -500, 500);
	b->cfg.mod_hud_y = std::clamp(geti(L"modifiers", L"hud_y", 0), -500, 500);
	b->cfg.mod_hud_scale = std::clamp(geti(L"modifiers", L"hud_scale", 100), 50, 150);

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
// online or replays). Changes take effect at once; Health from the next round (health_tick).
// Runs on the game thread, once per player per frame (decode_hook).
struct Mods { bool no_rage = false, no_pc = false, no_screw = false, no_wb = false; int health = 100, backdash = 100; } g_mods;

// Health multiplier (2026-10-25, a write watch on the health percent +0x1508: exe+0x742C70 computes it every
// frame as current * 100 / max). The health object is at player +0x14E0: max health at +0x14FC (16.16 fixed,
// 175.0; +0x14F8 and +0x1500 hold the same), current health ENCODED - check dword +0x14E8 with key qword
// +0x14F0, value = 0x255A60(check, key) (0 if the check fails = a KO), new check = 0x2D0EA0(value, key).
// We scale the max copies and keep the current health in step: whenever the game (re)sets the max (a new
// match / round) and whenever the health goes UP (round start, Practice refill), it becomes the scaled max.
constexpr uintptr_t kHealth = 0x14E0, kFnDecodeInt = 0x255A60, kFnEncodeInt = 0x2D0EA0;
struct HealthState { int32_t base = 0, target = 0, last = 0, seen = 0; } g_health[2];
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
	// per round (author, 2026-10-29): the slider's value is taken whenever the game sets the max (a new match /
	// round) or the health comes back to full (a round start, a Practice refill) - never in the middle of a round
	HealthState &st = g_health[idx];
	int32_t mx = 0;
	if (!t7::rd(p + 0x14FC, mx) || mx <= 0)
		return;
	const int32_t cur = health_get(p);
	const bool game_max = mx != st.seen;            // the game (re)set the max
	const bool refill = cur > st.last && cur >= mx;  // back to full
	if (game_max || refill)
	{
		const int32_t base = game_max || st.base <= 0 ? mx : st.base; // the unscaled max
		const int32_t target = int32_t(int64_t(base) * pct / 100);
		if (target != mx)
			for (const uintptr_t o : { 0x14F8, 0x14FC, 0x1500 })
				*reinterpret_cast<volatile int32_t *>(p + o) = target;
		if (cur >= std::min(mx, base) && cur != target)
			health_set(p, target);
		if (target != mx || (game_max && pct != 100))
			logf("mods: health P%d: max %d -> %d (x%d%%), current %d -> %d", idx + 1, base >> 16, target >> 16, pct, cur >> 16,
				health_get(p) >> 16);
		st = { base, target, health_get(p), target };
		return;
	}
	if (cur > st.last && cur < st.target)
	{
		// health went up (a Practice refill on its way): to the max
		health_set(p, st.target);
		logf("mods: health P%d: refill %d -> %d", idx + 1, cur >> 16, st.target >> 16);
	}
	st.last = health_get(p);
}
// Backdash distance (2026-10-28, Asuka's b,b sampled live, a write watch on her position, then her whole block every
// frame of a backdash): the fighter's position is +0xE0 / +0xE4 / +0xE8 (x, height, z), but during a move it stays
// at the move's start - the movement is the animation's root offset (+0x104 / +0x108 / +0x10C, along the facing),
// rotated and added on top (+0x160 etc.) and folded into +0xE0 when the move ends. The player update samples the
// offset from the animation every frame (exe+0x7663A6: the total since the move started, not a step) and hands it
// to exe+0x6E73C0(offset, player), the game's own root motion scaling by the character's size (horizontal x
// [0], [8], [0x10], [0x18]; vertical x [4], [0x14]). Hooked: after it, while that player is in their default
// backdash the horizontal parts are scaled by the slider too. The default backdash is "<xx>_sSTEPB" / "_sSTEPBLp"
// farther than 1800 from the opponent and "<xx>_sDASHB" / "_sDASHBLp" from 1800 or closer (the walk / stand
// cancels: requirement 36:1800 / 35:1800; the prefix is the character's: Jn_, He_ ...). Some characters (King, Jin)
// have them without a prefix as "sSTEP_00B" / "sSTEP_00BLp" and "sDASH_00B" / "sDASH_00BLp" (same requirements). Sidestep distance the same
// way (2026-10-28, Leo sampled live): a sidestep is "sSTEP_01L" (up) / "sSTEP_01R" (down), "sSTEP_21L/R" its
// variants - no prefix, and no other sidestep by distance. The same slider covers sidewalks (author): "sWALK_<2 digits>L/R"
// (00, 01, 02, 21, 51 - holding up / down; forward / back walking is sWALK_00FMv / 00BMv, left alone). Offline battles only (g_bd_pct is 100 anywhere else).
std::atomic<int> g_bd_pct[2] = { 100, 100 }, g_ss_pct[2] = { 100, 100 };
enum StepKind { kNoStep, kBackdash, kSidestep };
StepKind step_kind(uintptr_t p, int idx)
{
	static uintptr_t cache_move[2] = {};
	static StepKind cache_kind[2] = {};
	uint64_t mp = 0, np = 0;
	if (!t7::rd(p + 0x218, mp) || !mp) // +0x218 = the current move (its moveset entry)
		return kNoStep;
	if (uintptr_t(mp) == cache_move[idx])
		return cache_kind[idx];
	char nm[24] = {};
	cache_move[idx] = uintptr_t(mp), cache_kind[idx] = kNoStep;
	if (!t7::rd(uintptr_t(mp), np) || !t7::safe_copy(nm, uintptr_t(np), sizeof nm - 1))
		return kNoStep;
	const char *u = std::strchr(nm, '_');
	static const char *const backdash[] = { "sSTEPB", "sSTEPBLp", "sDASHB", "sDASHBLp" };              // after the prefix
	static const char *const backdash_plain[] = { "sSTEP_00B", "sSTEP_00BLp", "sDASH_00B", "sDASH_00BLp" }; // no prefix
	for (const char *b : backdash)
		if (u && std::strcmp(u + 1, b) == 0)
			cache_kind[idx] = kBackdash;
	for (const char *b : backdash_plain)
		if (std::strcmp(nm, b) == 0)
			cache_kind[idx] = kBackdash;
	if (cache_kind[idx] == kBackdash)
		return kBackdash;
	else if (std::strcmp(nm, "sSTEP_01L") == 0 || std::strcmp(nm, "sSTEP_01R") == 0 || std::strcmp(nm, "sSTEP_21L") == 0 ||
		std::strcmp(nm, "sSTEP_21R") == 0)
		cache_kind[idx] = kSidestep;
	else if (std::strncmp(nm, "sWALK_", 6) == 0 && std::isdigit(uint8_t(nm[6])) && std::isdigit(uint8_t(nm[7])) &&
		(nm[8] == 'L' || nm[8] == 'R') && nm[9] == 0)
		cache_kind[idx] = kSidestep; // a sidewalk
	return cache_kind[idx];
}
using RootScaleFn = void(__fastcall *)(float *, uintptr_t);
RootScaleFn g_root_scale_orig = nullptr;
void __fastcall root_scale_hook(float *off, uintptr_t player)
{
	g_root_scale_orig(off, player);
	const uintptr_t base = t7::exe_base();
	const int idx = player == base + t7::kLiveP1 ? 0 : player == base + t7::kLiveP2 ? 1 : -1;
	if (idx < 0)
		return;
	const int bd = g_bd_pct[idx].load(std::memory_order_relaxed), ss = g_ss_pct[idx].load(std::memory_order_relaxed);
	if (bd == 100 && ss == 100)
		return;
	const StepKind kind = step_kind(player, idx);
	const int pct = kind == kBackdash ? bd : kind == kSidestep ? ss : 100;
	if (pct == 100)
		return;
	const float k = float(pct) / 100.0f;
	off[0] *= k, off[2] *= k, off[4] *= k, off[6] *= k;
}
void install_root_scale()
{
	static const uint8_t pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40 }; // mov [rsp+8],rbx; push rdi; sub rsp,0x40
	auto *fn = reinterpret_cast<void *>(t7::exe_base() + 0x6E73C0);
	if (std::memcmp(fn, pro, sizeof pro) != 0 || MH_CreateHook(fn, reinterpret_cast<void *>(&root_scale_hook),
		reinterpret_cast<void **>(&g_root_scale_orig)) != MH_OK || MH_EnableHook(fn) != MH_OK)
		logf("mods: root motion hook failed (Backdash / Sidestep Distance have no effect)");
}
// Camera distance (2026-10-28): the battle camera views are 0x38-byte slots in .data (eye +0, look-at +0x10, up +0x20,
// fov +0x30), copied in by exe+0x6E2030(dst, src) (then validated; an invalid one becomes a default), ~40 a frame.
// Writing them from outside changed nothing - recomputed and used within a frame - so the setter is hooked. The
// pipeline (author's tests + per-frame recordings): exe+0x34DE6F0 = the smoothed battle camera -> exe+0x34DE900 = it,
// or a cinematic (Rage Arts...) blended in -> exe+0x34DE4F0 = that plus the hit effects (heavy moves: the fov punches
// 65 -> 35..45 with a slight dolly; the Rage Art intro shots: their own close distance) = what is rendered. Only that
// final view is changed, by how much of it is the battle camera: stage 2 like stage 1 (not a cinematic) and no hit
// effect on top (its fov below stage 2's - 10 degrees = none of it). So heavy moves zoom from the user's distance into
// the game's own close-up, Rage Arts keep their angles; a cinematic drops the share at once, and coming back it eases in
// (8% a frame) so the camera zooms out smoothly. The height offset moves the eye and the look-at up together (a straight
// lift, no tilt - the author's spec) with the same weight. Every game mode, online too (author, 2026-11-02): it only changes what this
// screen shows - nothing of the fight.
std::atomic<int> g_cam_pct{ 100 }, g_cam_height{ 0 }; // height in mm (the views are in mm)
std::atomic<float> g_cam_share{ 1.0f };                // how much of the final view is the battle camera (eased)
using CamSetFn = int64_t(__fastcall *)(float *, const float *);
CamSetFn g_cam_set_orig = nullptr;
int64_t __fastcall cam_set_hook(float *dst, const float *src)
{
	const int pct = g_cam_pct.load(std::memory_order_relaxed), height_mm = g_cam_height.load(std::memory_order_relaxed);
	const uintptr_t base = t7::exe_base();
	if ((pct == 100 && height_mm == 0) || reinterpret_cast<uintptr_t>(dst) != base + 0x34DE4F0)
		return g_cam_set_orig(dst, src);
	// the changed view is built first and handed to the game's own copy, so the final view never holds the unchanged
	// one in between (2026-11-02 recording: single frames at 100% during a held Rage Art shot)
	float v[14];
	std::memcpy(v, src, sizeof v);
	float share = g_cam_share.load(std::memory_order_relaxed);
	float st1[13] = {}, st2[13] = {};
	if (!t7::safe_copy(st1, base + 0x34DE6F0, sizeof st1) || !t7::safe_copy(st2, base + 0x34DE900, sizeof st2))
		return g_cam_set_orig(dst, src);
	float diff = 0;
	for (int i = 0; i < 3; ++i)
		diff += std::fabs(st1[i] - st2[i]) + std::fabs(st1[4 + i] - st2[4 + i]);
	const float battle = std::clamp(1.0f - diff / 500.0f, 0.0f, 1.0f) * std::clamp(1.0f - std::fabs(st1[12] - st2[12]) / 10.0f, 0.0f, 1.0f);
	share = battle < share ? battle : share + (battle - share) * 0.08f;
	g_cam_share.store(share, std::memory_order_relaxed);
	// a zoom-in (heavy hits, Rage Art close-ups: at or closer than stage 2) is left to the game; a WIDER shot with a
	// narrower fov is the round-start shot ("Round 2... Fight!": 5000 at fov 54 over a 3400 battle camera, 2026-11-02
	// recording) and keeps the player's distance and height
	auto dist = [](const float *w) { return std::sqrt((w[0] - w[4]) * (w[0] - w[4]) + (w[1] - w[5]) * (w[1] - w[5]) + (w[2] - w[6]) * (w[2] - w[6])); };
	const bool wide = dist(v) > dist(st2) * 1.2f;
	const float effect_now = wide ? 0.0f : std::clamp((st2[12] - v[12]) / 10.0f, 0.0f, 1.0f);
	// eased a quarter of the way a frame (author: a short fov punch in Leo's Rage Art start snapped to 100% in 2 frames)
	static float effect = 0.0f;
	effect += (effect_now - effect) * 0.25f;
	// the slider's percent maps to 1 + (pct - 100%) * 0.75 (author: 200% = 1.75x, 25% = 0.44x - the full range felt too strong)
	const float weight = share * (1.0f - effect);
	const float g = 1.0f + (float(pct) / 100.0f - 1.0f) * 0.75f * weight;
	for (int i = 0; i < 3; ++i)
		v[i] = v[4 + i] + (v[i] - v[4 + i]) * g;
	const float lift = float(height_mm) * weight; // +y = up
	v[1] += lift, v[5] += lift;                     // eye and look-at together: a straight lift, no tilt (author)
	return g_cam_set_orig(dst, v);
}
// The round-start camera (exe+0x6F1F40, this = the camera object; Ghost Battle's "Round 1/2... Fight!", found with
// a read watch on the final view's eye height, 2026-11-02): it takes the final view's distance and angle but
// recomputes the look-at from the fighters (exe+0x72BCB0 -> +0xC0) and the eye from it (+0xB0) every frame - so it
// keeps the Camera Distance and drops the lift. Add the lift to both after it (no tilt, nothing accumulates).
using RoundCamFn = int64_t(__fastcall *)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
RoundCamFn g_round_cam_orig = nullptr;
int64_t __fastcall round_cam_hook(uintptr_t self, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	const int64_t r = g_round_cam_orig(self, a2, a3, a4);
	const float lift = float(g_cam_height.load(std::memory_order_relaxed)) * g_cam_share.load(std::memory_order_relaxed);
	float eye_y = 0, at_y = 0;
	if (lift != 0.0f && t7::rd(self + 0xB4, eye_y) && t7::rd(self + 0xC4, at_y))
	{
		*reinterpret_cast<volatile float *>(self + 0xB4) = eye_y + lift;
		*reinterpret_cast<volatile float *>(self + 0xC4) = at_y + lift;
	}
	return r;
}
void install_cam_set()
{
	static const uint8_t pro[] = { 0x48, 0x83, 0xEC, 0x28, 0x0F, 0x10, 0x02, 0x0F, 0x11, 0x01 }; // sub rsp,0x28; movups xmm0,[rdx]; movups [rcx],xmm0
	auto *fn = reinterpret_cast<void *>(t7::exe_base() + 0x6E2030);
	if (std::memcmp(fn, pro, sizeof pro) != 0 || MH_CreateHook(fn, reinterpret_cast<void *>(&cam_set_hook),
		reinterpret_cast<void **>(&g_cam_set_orig)) != MH_OK || MH_EnableHook(fn) != MH_OK)
		logf("mods: camera hook failed (Camera Distance has no effect)");
	static const uint8_t rpro[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18 }; // mov rax,rsp; mov [rax+10],rbx; mov [rax+18],rsi
	auto *rf = reinterpret_cast<void *>(t7::exe_base() + 0x6F1F40);
	if (std::memcmp(rf, rpro, sizeof rpro) != 0 || MH_CreateHook(rf, reinterpret_cast<void *>(&round_cam_hook),
		reinterpret_cast<void **>(&g_round_cam_orig)) != MH_OK || MH_EnableHook(rf) != MH_OK)
		logf("mods: round-start camera hook failed (the height is not kept in round-start shots)");
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

// No Wall Bound (2026-10-28, every-frame recording of Leo's f+1+2 at the wall and mid-stage, then the movesets read
// live): hit reactions play from the ATTACKER's moveset (player +0x1520 = its header: +0x1B0 cancels, +0x210 moves,
// 0xB0 each, +0x20 = the move's cancel list, 0x28 per cancel, +8 requirements, +0x24 target move, 0x8000 ends a list).
// A wall bound hit puts the victim in a "PIN" flight (sDw_katay_PIN + L/R) whose cancels go to the wall bound moves
// sDm_PNGB_PON* when the wall is touched (requirement 182 = wall direction 2/4/8); a normal blowback (sDw_dwhiji)
// has the same requirements but goes to the wall splats wDm_*. With the modifier those PON cancels go to the splat
// of the same direction and timing (34108 = the early window) - patched only in offline battles (game mode 2),
// restored the first frame of anything else. Every character's moveset has the same reaction names.
struct WbPatch { uintptr_t at; uint16_t orig, repl; };
struct WbSet { uintptr_t moveset = 0; std::vector<WbPatch> p; } g_wb[2];
bool wb_name(uintptr_t move, char (&out)[24])
{
	uint64_t np = 0;
	out[0] = 0;
	if (!t7::rd(move, np) || !t7::safe_copy(out, uintptr_t(np), sizeof out - 1))
		return false;
	out[sizeof out - 1] = 0;
	return true;
}
// a cancel's wall key: requirement 182's direction, plus 0x100 when 34108 (the early window) is required too
int wb_key(uintptr_t cancel)
{
	uint64_t rq = 0;
	if (!t7::rd(cancel + 8, rq) || !rq)
		return -1;
	int dir = -1, early = 0;
	for (int k = 0; k < 24; ++k)
	{
		uint32_t r = 0, v = 0;
		if (!t7::rd(uintptr_t(rq) + 8 * k, r) || !t7::rd(uintptr_t(rq) + 8 * k + 4, v) || r == 881)
			break;
		if (r == 182)
			dir = int(v);
		else if (r == 34108)
			early = 0x100;
	}
	return dir < 0 ? -1 : dir | early;
}
void wb_patch(bool on)
{
	const uintptr_t base = t7::exe_base();
	for (int i = 0; i < 2; ++i)
	{
		WbSet &s = g_wb[i];
		uint64_t ms = 0;
		t7::rd(base + (i ? t7::kLiveP2 : t7::kLiveP1) + 0x1520, ms);
		if (s.moveset && (!on || ms != s.moveset))
		{
			// off, or another moveset now: put ours back where it is still ours (a freed moveset is not touched)
			int back = 0;
			if (ms == s.moveset)
				for (const WbPatch &pt : s.p)
				{
					uint16_t cur = 0;
					if (t7::rd(pt.at, cur) && cur == pt.repl)
						*reinterpret_cast<volatile uint16_t *>(pt.at) = pt.orig, ++back;
				}
			if (back)
				logf("mods: no wall bound P%d: %d cancels restored", i + 1, back);
			s = {};
		}
		if (!on || !ms || ms == s.moveset || ms == g_wb[i ^ 1].moveset)
			continue; // nothing to do, done, or the other side has the same moveset
		s.moveset = uintptr_t(ms);
		uint64_t moves = 0, nmoves = 0;
		if (!t7::rd(s.moveset + 0x210, moves) || !t7::rd(s.moveset + 0x218, nmoves) || !moves || nmoves == 0 || nmoves > 10000)
			continue;
		// the moves by name: the wall bound targets (PON) and the normal blowback the splats are taken from
		std::vector<uint8_t> pon(size_t(nmoves), 0);
		int64_t ref = -1;
		char nm[24];
		for (uint64_t m = 0; m < nmoves; ++m)
			if (wb_name(uintptr_t(moves + m * 0xB0), nm))
			{
				if (std::strncmp(nm, "sDm_PNGB_PON", 12) == 0)
					pon[size_t(m)] = 1;
				else if (ref < 0 && std::strcmp(nm, "sDw_dwhiji") == 0)
					ref = int64_t(m);
			}
		auto list = [&](uint64_t m, auto &&fn) {
			uint64_t c = 0;
			if (!t7::rd(uintptr_t(moves + m * 0xB0 + 0x20), c) || !c)
				return;
			for (int k = 0; k < 200; ++k, c += 0x28)
			{
				uint64_t cmd = 0;
				uint16_t to = 0;
				if (!t7::rd(uintptr_t(c), cmd) || !t7::rd(uintptr_t(c + 0x24), to))
					return;
				fn(uintptr_t(c), to);
				if (cmd == 0x8000)
					return;
			}
		};
		std::unordered_map<int, uint16_t> splat; // wall key -> the normal splat move
		if (ref >= 0)
			list(uint64_t(ref), [&](uintptr_t c, uint16_t to) {
				if (to < nmoves && wb_name(uintptr_t(moves + to * 0xB0), nm) && std::strncmp(nm, "wDm_", 4) == 0)
					splat.emplace(wb_key(c), to);
			});
		if (splat.empty())
		{
			logf("mods: no wall bound P%d: no normal wall splat reaction found, left as it is", i + 1);
			continue;
		}
		for (uint64_t m = 0; m < nmoves; ++m)
			list(m, [&](uintptr_t c, uint16_t to) {
				if (to >= nmoves || !pon[to])
					return;
				const int key = wb_key(c);
				auto it = splat.find(key);
				if (it == splat.end())
					it = splat.find(key & 0xFF); // no early-window splat: the later one
				if (it != splat.end())
					s.p.push_back({ c + 0x24, to, it->second }); // the cancel's target move
			});
		for (const WbPatch &pt : s.p)
			*reinterpret_cast<volatile uint16_t *>(pt.at) = pt.repl;
		logf("mods: no wall bound P%d: %zu wall bound cancels -> wall splats", i + 1, s.p.size());
	}
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
	// the checkboxes take effect immediately, Health from the next round (author, 2026-10-29; they used to be latched
	// per match) - offline battles only, as before
	if (idx == 0 && mode == t7::kModePractice && b)
	{
		const Mods was = g_mods;
		g_mods.no_rage = b->cfg.mod_no_rage;
		g_mods.health = b->cfg.mod_health;
		g_mods.no_pc = b->cfg.mod_no_pc;
		g_mods.no_screw = b->cfg.mod_no_screw;
		g_mods.no_wb = b->cfg.mod_no_wall_bound;
		g_mods.backdash = b->cfg.mod_backdash;
		const bool changed = was.no_rage != g_mods.no_rage || was.no_pc != g_mods.no_pc || was.no_screw != g_mods.no_screw ||
			was.no_wb != g_mods.no_wb || was.health != g_mods.health;
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
		if (changed || new_match)
			logf("mods: %s: no rage %s, health %d%% (from the next round), no power crush %s, no screw %s, no wall bound %s",
				new_match ? "new match" : "changed", g_mods.no_rage ? "on" : "off", g_mods.health, g_mods.no_pc ? "on" : "off",
				g_mods.no_screw ? "on" : "off", g_mods.no_wb ? "on" : "off");
	}
	pc_patch(mode == t7::kModePractice && g_mods.no_pc);
	if (idx == 0)
		wb_patch(mode == t7::kModePractice && g_mods.no_wb);
#ifndef T7_PUBLIC
	// No Screw recon (2026-10-25): costume_watch_char=800 - record P2's whole player block (0x3670) every frame
	// with P1's move id, for 3 minutes (frames_p2.bin: per frame u32 n, i32 p1 move, i32 p2 move, block)
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
			if (n >= 60 * 180)
				fclose(rec), rec = nullptr, logf("screw recon: %u frames recorded", n);
		}
	}
#endif
	if (idx == 0) // the camera in every mode (it changes only what this screen shows)
	{
		// the height: linked to the distance by default (author: 100% = 0 cm, 120% = +5 cm, linear = 0.25 cm per percent),
		// or the player's own value when "Adjust camera height independently" is ticked
		const int pct = b ? b->cfg.mod_camera : 100;
		g_cam_pct = pct;
		g_cam_height = b && b->cfg.mod_camera_height_free ? b->cfg.mod_camera_height * 10 : (pct - 100) * 10 / 4; // mm
	}
	if (mode != t7::kModePractice)
	{
		g_bd_pct[idx] = 100, g_ss_pct[idx] = 100;
		return;
	}
	health_tick(p, idx, g_mods.health);
	g_bd_pct[idx] = b ? b->cfg.mod_backdash : 100; // the sliders' values right away (author), not latched
	g_ss_pct[idx] = b ? b->cfg.mod_sidestep : 100;
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
#include "hud.hpp"
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
	install_root_scale();
	install_cam_set();
	hud::install();
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
		if (hotkey(VK_END)) gbm::g_ss_req = true, logf("ss test: requested (Ctrl+End)");
		// dialog_watch_tick(); // done (2026-10-19): readers found; the getter hooks in menutrace.hpp log the owners
#endif
		g_tracer.tick();
#ifndef T7_PUBLIC
		if (ghost::Brain *wb = g_brain)
			gb::slotwatch::costume_watch_tick(wb->cfg.gb_costume_watch_char);
		gb::slotwatch::tick();
		gb::simple_select_tick();
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
